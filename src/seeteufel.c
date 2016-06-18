#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#define CMD "Seeteufel"
static void usage(char *cmd)
{
    fprintf(stderr, "Usage: %s [-d] [--] [command]\n", cmd);
    fprintf(stderr, "Command:\n");
    fprintf(stderr, "  shutdown         : shutdown daemon\n");
    fprintf(stderr, "  change LEFT RIGHT: change gears left and right\n");
    fprintf(stderr, "  left LEFT        : change gear left only\n");
    fprintf(stderr, "  right RIGHT      : change gear right only\n");
}

static int send_msg(char *msg, size_t len)
{
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket failed");
        return 1;
    }

    struct sockaddr_un sa = {0};
    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, "/tmp/" CMD "-sock");

    if (connect(sock, (struct sockaddr*)&sa,
                sizeof(struct sockaddr_un)) == -1) {
        perror("connect failed");
        goto error;
    }
    if (write(sock, msg, len) == -1) {
        perror("write failed");
        goto error;
    }

    close(sock);
    return 0;

error:
    close(sock);
    return 1;
}

static int client(int argc, char **argv)
{
    int left, right = 0;
    char msg[1024];
    if (strcmp("s", argv[0]) <= 0) {
        snprintf(msg, sizeof(msg), "shutdown");
        return send_msg(msg, strlen(msg) + 1);
    } else if (strcmp("r", argv[0]) <= 0) {
        if (argc < 2) {
            usage(CMD);
            exit(EXIT_FAILURE);
        }
        right = atoi(argv[1]);
        snprintf(msg, sizeof(msg), "right:%d", right);
        return send_msg(msg, strlen(msg) + 1);
    } else if (strcmp("l", argv[0]) <= 0) {
        if (argc < 2) {
            usage(CMD);
            exit(EXIT_FAILURE);
        }
        left = atoi(argv[1]);
        snprintf(msg, sizeof(msg), "left:%d", left);
        return send_msg(msg, strlen(msg) + 1);
    } else if (strcmp("c", argv[0]) <= 0) {
        if (argc < 3) {
            usage(CMD);
            exit(EXIT_FAILURE);
        }
        left = atoi(argv[1]);
        right = atoi(argv[2]);
        snprintf(msg, sizeof(msg), "change:%d,%d", left, right);
        return send_msg(msg, strlen(msg) + 1);
    }

    usage(CMD);
    exit(EXIT_FAILURE);
}

volatile unsigned *gpio_base;
#define GPIO_SET *(gpio_base+7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR *(gpio_base+10) // clears bits which are 1 ignores bits which are 0

static void gpio_set_direction(int gpio, int direction)
{
    *(gpio_base+((gpio)/10)) &= ~(7<<(((gpio)%10)*3));
    *(gpio_base+((gpio)/10)) |=  (1<<(((gpio)%10)*3));
}

struct engine_settings {
    int in1;
    int in2;
    int on;
    int change;
    int value;
    int up;
    int down;
    int pitch;
};
static struct engine_settings e_left = {
    .in1 = 27,
    .in2 = 17,
    .on = 1,
    .change = 0,
    .value = 0,
    .up = 0,
    .down = 100,
    .pitch = 100,
};
static struct engine_settings e_right = {
    .in1 = 24,
    .in2 = 23,
    .on = 1,
    .change = 0,
    .value = 0,
    .up = 0,
    .down = 100,
    .pitch = 100,
};

static void set_duty(struct engine_settings *e, int level)
{
    if (level > e->pitch) level = e->pitch;
    if (level < -e->pitch) level = -e->pitch;
    if (level < 0) {
        e->value = 1 << e->in2;
        level = -level;
    } else if (level > 0) {
        e->value = 1 << e->in1;
    } else {
        e->value = 1 << e->in1 | 1 << e->in2;
    }

    e->up = level;
    e->down = e->pitch - level;
    e->change = 1;
}

static void *engine(void *arg)
{
    struct engine_settings *e = (struct engine_settings *)arg;

    // set out direction
    gpio_set_direction(e->in1, 1);
    gpio_set_direction(e->in2, 1);

    while (e->on) {
        if (e->change) {
            GPIO_CLR = 1 << e->in1;
            GPIO_CLR = 1 << e->in2;
            e->change = 0;
        }
        if (e->up > 0) {
            GPIO_SET = e->value;
            usleep(e->up * 100);
        }
        if (e->down > 0) {
            GPIO_CLR = e->value;
            usleep(e->down * 100);
        }
    }

    return NULL;
}


int server()
{
    int ret = 0;
    int gpiomem_fd;
    pthread_t engine_l, engine_r;

    openlog("Seeteufel", LOG_PID, LOG_DAEMON);

    if (daemon(0, 0) == -1) {
        syslog(LOG_ERR, "failed to launch daemon.\n");
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "daemon started.\n");

    if ((gpiomem_fd = open("/dev/gpiomem", O_RDWR|O_SYNC) ) < 0) {
        syslog(LOG_ERR, "can't open /dev/gpiomem: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    void *gpio_map = mmap(NULL, sysconf(_SC_PAGE_SIZE), PROT_READ|PROT_WRITE,
                          MAP_SHARED, gpiomem_fd, 0);
    close(gpiomem_fd);
    if (gpio_map == MAP_FAILED) {
        syslog(LOG_ERR, "mmap() failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    gpio_base = (volatile unsigned *)gpio_map;

    // create domain socket
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) {
        syslog(LOG_ERR, "socket() failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    struct sockaddr_un sa = {0};
    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, "/tmp/" CMD "-sock");

    remove(sa.sun_path);

    if (bind(sock, (struct sockaddr*)&sa, sizeof(struct sockaddr_un)) == -1) {
        syslog(LOG_ERR, "bind() failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (listen(sock, 128) == -1) {
        syslog(LOG_ERR, "listen() failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }


    // start engine thread
    e_left.on = 1;
    if (pthread_create(&engine_l, NULL, &engine, &e_left) != 0) {
        syslog(LOG_ERR, "pthread_create() failed\n");
        exit(EXIT_FAILURE);
    }
    e_right.on = 1;
    if (pthread_create(&engine_r, NULL, &engine, &e_right) != 0) {
        syslog(LOG_ERR, "pthread_create() failed\n");
        exit(EXIT_FAILURE);
    }

    // event loop
    while (1) {
        char buffer[4096];
        int fd = accept(sock, NULL, NULL);
        if (fd == -1) {
            syslog(LOG_ERR, "listen() failed: %s\n", strerror(errno));
            ret = -1;
            goto finish;
        }

        int recv_size = read(fd, buffer, sizeof(buffer) - 1);
        if (recv_size == -1) {
            syslog(LOG_ERR, "read() failed: %s\n", strerror(errno));
            ret = -1;
            goto finish;
        }

        if (close(fd) == -1) {
            syslog(LOG_ERR, "close() failed: %s\n", strerror(errno));
            ret = -1;
            goto finish;
        }

        buffer[recv_size] = '\0';
        syslog(LOG_INFO, "message: %s\n", buffer);

        if (strcmp("s", buffer) <= 0) {
            syslog(LOG_INFO, "shutdown daemon..\n");
            set_duty(&e_left, 0);
            set_duty(&e_right, 0);
            break;
        } else if (strcmp("r", buffer) <= 0) {
            int right = 0;
            if (sscanf(buffer, "right:%d", &right) == 1) {
                set_duty(&e_right, right);
            }
        } else if (strcmp("l", buffer) <= 0) {
            int left = 0;
            if (sscanf(buffer, "left:%d", &left) == 1) {
                set_duty(&e_left, left);
            }
        } else if (strcmp("c", buffer) <= 0) {
            int left = 0, right = 0;
            if (sscanf(buffer, "change:%d,%d", &left, &right) == 2) {
                set_duty(&e_left, left);
                set_duty(&e_right, right);
            }
        }
    }

    e_left.on = 0;
    e_right.on = 0;
    pthread_join(engine_l, NULL);
    pthread_join(engine_r, NULL);

finish:
    close(sock);
    remove(sa.sun_path);
    syslog(LOG_INFO, "daemon finished.\n");
    closelog();
    return ret;
}

int main(int argc, char **argv)
{
    int opt;

    while ((opt = getopt(argc, argv, "d")) != -1) {
        switch (opt) {
            case 'd':
                return server();
            default: /* '?' */
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    argc -= optind;
    argv += optind;

    return client(argc, argv);
}
