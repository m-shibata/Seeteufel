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
#include <netdb.h>
#include <sys/select.h>

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

enum event_id {
    EVENT_HANDLED,
    EVENT_DISCONNECT,
    EVENT_SHUTDOWN,
    EVENT_ERROR = -1,
};

enum event_id handle_event(int fd)
{
    char buffer[4096];
    int recv_size = read(fd, buffer, sizeof(buffer) - 1);
    if (recv_size == -1) {
        syslog(LOG_ERR, "read() failed: %s\n", strerror(errno));
        return EVENT_ERROR;
    } else if (recv_size == 0) {
        return EVENT_HANDLED;
    }

    buffer[recv_size] = '\0';
    syslog(LOG_INFO, "message: %s\n", buffer);

    if (strcmp("s", buffer) <= 0) {
        syslog(LOG_INFO, "shutdown daemon..\n");
        set_duty(&e_left, 0);
        set_duty(&e_right, 0);
        return EVENT_SHUTDOWN;
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
    } else if (strcmp("d", buffer) <= 0) {
        syslog(LOG_INFO, "disconnect client..\n");
        set_duty(&e_left, 0);
        set_duty(&e_right, 0);
        return EVENT_DISCONNECT;
    } else if (strcmp("c", buffer) <= 0) {
        int left = 0, right = 0;
        if (sscanf(buffer, "change:%d,%d", &left, &right) == 2) {
            set_duty(&e_left, left);
            set_duty(&e_right, right);
        }
    }
    return EVENT_HANDLED;
}

#define PORT "51010"
int eventLoopTCP()
{
    int ret = 0;
    struct addrinfo hints;
    struct addrinfo *res, *rp;
    int i, sock[FD_SETSIZE], sock_id = 0, max_sock = 0;
    fd_set rfds;


    FD_ZERO(&rfds);
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    ret = getaddrinfo(NULL, PORT, &hints, &res);
    if (ret != 0) {
        syslog(LOG_ERR, "getaddrinfo() failed: %s\n", gai_strerror(ret));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock[sock_id] = socket(rp->ai_family, rp->ai_socktype,
                               rp->ai_protocol);
        if (sock[sock_id] == -1)
            continue;

        if (bind(sock[sock_id], rp->ai_addr, rp->ai_addrlen) != 0) {
            close(sock[sock_id]);
            continue;
        }

        if (listen(sock[sock_id], 1) == -1) {
            close(sock[sock_id]);
            continue;
        }

        if (sock[sock_id] > max_sock) max_sock = sock[sock_id];
        FD_SET(sock[sock_id], &rfds);
        if (++sock_id >= FD_SETSIZE)
            break;
    }

    freeaddrinfo(res);

    if (sock_id == 0) {
        syslog(LOG_ERR, "no socket\n");
        return -1;
    }

    syslog(LOG_INFO, "connected\n");
    while (1) {
        ret = select(max_sock + 1, &rfds, NULL, NULL, NULL);
        if (ret < 0) {
            if (errno == EINTR) continue;
            syslog(LOG_ERR, "select() failed: %s\n", strerror(errno));
            ret = -1;
            goto finish;
        } else if (ret == 0)
            continue;

        for (i = 0; i < sock_id; i++) {
            if (FD_ISSET(sock[i], &rfds)) {
                int fd = accept(sock[i], NULL, NULL);
                if (fd == -1) {
                    syslog(LOG_ERR, "listen() failed: %s\n", strerror(errno));
                    ret = -1;
                    goto finish;
                }

                int loop_on = 1;
                while (loop_on) {
                    enum event_id ev = handle_event(fd);
                    if (ev != EVENT_HANDLED) {
                        if (ev == EVENT_SHUTDOWN) {
                            ret = 0;
                            close(fd);
                            goto finish;
                        } else if (ev == EVENT_DISCONNECT)
                            loop_on = 0;
                    }
                }

                if (close(fd) == -1) {
                    syslog(LOG_ERR, "close() failed: %s\n", strerror(errno));
                    ret = -1;
                    goto finish;
                }
                break;
            }
        }
    }

finish:
    for (i = 0; i < sock_id; i++)
        close(sock[sock_id]);
    return ret;
}

int create_unix_domain(const char *path)
{
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) {
        syslog(LOG_ERR, "socket() failed: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_un sa = {0};
    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, path);

    remove(sa.sun_path);

    if (bind(sock, (struct sockaddr*)&sa, sizeof(struct sockaddr_un)) == -1) {
        syslog(LOG_ERR, "bind() failed: %s\n", strerror(errno));
        close(sock);
        return -1;
    }

    if (listen(sock, 128) == -1) {
        syslog(LOG_ERR, "listen() failed: %s\n", strerror(errno));
        close(sock);
        return -1;
    }

    return sock;
}

int eventLoopUDS()
{
    int ret = 0;
    int sock = create_unix_domain("/tmp/" CMD "-sock");
    if (sock < 0) {
        syslog(LOG_ERR, "create_unix_domain() failed\n");
        exit(EXIT_FAILURE);
    }

    while (1) {
        int fd = accept(sock, NULL, NULL);
        if (fd == -1) {
            syslog(LOG_ERR, "listen() failed: %s\n", strerror(errno));
            ret = -1;
            break;
        }

        enum event_id ev = handle_event(fd);
        if (ev != EVENT_HANDLED) {
            close(fd);
            ret = 0;
            break;
        }

        if (close(fd) == -1) {
            syslog(LOG_ERR, "close() failed: %s\n", strerror(errno));
            ret = -1;
            break;
        }
    }

    close(sock);
    remove("/tmp/" CMD "-sock");
    return ret;
}

int server(int use_tcp)
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

    if (use_tcp) {
        ret = eventLoopTCP();
    } else {
        ret = eventLoopUDS();
    }

    e_left.on = 0;
    e_right.on = 0;
    pthread_join(engine_l, NULL);
    pthread_join(engine_r, NULL);

    syslog(LOG_INFO, "daemon finished.\n");
    closelog();
    return ret;
}

int main(int argc, char **argv)
{
    int opt;
    int is_daemon = 0, use_tcp = 0;

    while ((opt = getopt(argc, argv, "dt")) != -1) {
        switch (opt) {
            case 'd':
                is_daemon = 1;
                break;
            case 't':
                use_tcp = 1;
                break;
            default: /* '?' */
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (is_daemon)
        return server(use_tcp);

    if (optind >= argc) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    argc -= optind;
    argv += optind;

    return client(argc, argv);
}
