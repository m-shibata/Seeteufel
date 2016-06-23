#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/joystick.h>

#define BUF_SIZE 500
#define PORT "51010"

void print_gear(int gear_l, int gear_r)
{
    printf("GEAR: L=%4d R=%4d ", gear_l, gear_r);
    if (gear_l == 0 && gear_r == 0) {
        printf("(stop)\n");
    } else if (gear_l == gear_r) {
        if (gear_l > 0)
            printf("(forward)\n");
        else
            printf("(back)\n");
    } else if (gear_l > gear_r) {
        printf("(right)\n");
    } else {
        printf("(left)\n");
    }
}

void event_loop(int fd, int input)
{
    char msg[BUF_SIZE];
    size_t len;

    int loop_on = 1;
    int gear_l = 0, gear_r = 0;
    while (loop_on) {
        struct js_event event;
        int new_gear;
        if (read(input, &event, sizeof(struct js_event))
                >= sizeof(struct js_event)) {
            switch (event.type & 0x7f) {
                case JS_EVENT_BUTTON:
                    /* 0: squre, 1: cross, 2: circle, 3: triangle */
                    if (event.number == 0 && event.value == 1)
                        loop_on = 0; /* squre button */
                    break;
                case JS_EVENT_AXIS:
                    /*
                     * 1: left stick (up/down), 5: right stick (up/down)
                     *   0-32767, negative: far side
                     * 0: left stick (left/right), 2: right stick (left/right)
                     * 6: left right cursor
                     * 7: up down cursor
                     * 3: L2, 4: R2
                     */
                    new_gear = -(event.value / 3000) * 10;
                    if (event.number == 1) {
                        if (gear_l != new_gear) {
                            print_gear(new_gear, gear_r);
                            snprintf(msg, BUF_SIZE -1, "left:%d", new_gear);
                            len = strnlen(msg, BUF_SIZE - 1);
                            msg[len] = '\0';
                            if (write(fd, msg, len) != len) {
                                fprintf(stderr, "partial/failed write\n");
                            }
                            gear_l = new_gear;
                        }
                    } else if (event.number == 5) {
                        if (gear_r != new_gear) {
                            print_gear(gear_l, new_gear);
                            snprintf(msg, BUF_SIZE -1, "right:%d", new_gear);
                            len = strnlen(msg, BUF_SIZE - 1);
                            msg[len] = '\0';
                            if (write(fd, msg, len) != len) {
                                fprintf(stderr, "partial/failed write\n");
                            }
                            gear_r = new_gear;
                        }
                    }
                    break;
            }
        }
    }

    snprintf(msg, BUF_SIZE -1, "change:0,0");
    len = strnlen(msg, BUF_SIZE - 1);
    msg[len] = '\0';
    if (write(fd, msg, len) != len) {
        fprintf(stderr, "partial/failed write\n");
    }

    printf("disconnect\n");
    strncpy(msg, "disconnect", BUF_SIZE - 1);
    len = strnlen(msg, BUF_SIZE - 1);
    msg[len] = '\0';
    if (write(fd, msg, len) != len) {
        fprintf(stderr, "partial/failed write\n");
    }
}

int main(int argc, char *argv[])
{
    int input;
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int sfd, s;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s host inputdev\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    input = open(argv[2], O_RDONLY);
    if (input < 0) {
        fprintf(stderr, "failed to open %s", argv[2]);
        exit(EXIT_FAILURE);
    }
    /* delete calibration data */


    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    s = getaddrinfo(argv[1], PORT, &hints, &result);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        exit(EXIT_FAILURE);
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype,
                rp->ai_protocol);
        if (sfd == -1)
            continue;

        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break;

        close(sfd);
    }

    if (rp == NULL) {
        fprintf(stderr, "Could not connect\n");
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(result);

    event_loop(sfd, input);

    close(input);
    return 0;
}

