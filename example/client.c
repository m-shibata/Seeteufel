#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/input.h>
#include <linux/joystick.h>

#define BUF_SIZE 500
#define PORT "51010"

static void change_gear(int fd, int gear_l, int gear_r)
{
    char msg[BUF_SIZE];
    size_t len;
    ssize_t ret;

    printf("\rGEAR: L=%4d R=%4d ", gear_l, gear_r);
    if (gear_l == 0 && gear_r == 0) {
        printf("%-10s\n", "(stop)");
    } else if (gear_l == gear_r) {
        if (gear_l > 0)
            printf("%-10s\n", "(forward)");
        else
            printf("%-10s\n", "(back)");
    } else if (gear_l > gear_r) {
        printf("%-10s\n", "(right)");
    } else {
        printf("%-10s\n", "(left)");
    }

    snprintf(msg, BUF_SIZE -1, "change:%d,%d", gear_l, gear_r);
    len = strnlen(msg, BUF_SIZE - 1);
    msg[len] = '\0';
    ret = write(fd, msg, len);
    if (ret < 0 || (size_t)ret != len) {
        fprintf(stderr, "partial/failed write\n");
    }
}

static int handle_js_event(int fd, struct js_event event)
{
    int new_gear;
    static int gear_l = 0, gear_r = 0;

    switch (event.type & 0x7f) {
        case JS_EVENT_BUTTON:
            /* 0: squre, 1: cross, 2: circle, 3: triangle */
            if (event.number == 0 && event.value == 1) /* squre button */
                return 0;
            if (event.number == 1 && event.value == 1) { /* cross button */
                change_gear(fd, 0, 0);
                gear_l = gear_r = 0;
            }
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
                    change_gear(fd, new_gear, gear_r);
                    gear_l = new_gear;
                }
            } else if (event.number == 5) {
                if (gear_r != new_gear) {
                    change_gear(fd, gear_l, new_gear);
                    gear_r = new_gear;
                }
            }
            break;
    }

    return 1;
}

uint16_t keymap[KEY_MAX - BTN_MISC + 1];
int16_t keyabs[ABS_CNT];

static int create_js_event(struct input_event event, struct js_event *js_event)
{
    if (!js_event)
        return -1;

    switch (event.type) {
        case EV_KEY:
            if (event.code < BTN_MISC || event.value == 2)
                break; /* ignore keyboard or auto repeat */
            js_event->type = JS_EVENT_BUTTON;
            js_event->number = keymap[event.code - BTN_MISC];
            js_event->value = event.value; /* press: 1, release: 0 */
            break;

        case EV_ABS:
            /* left: 1, right: 5 (far 0, near 255) */
            js_event->type = JS_EVENT_AXIS;
            js_event->number = event.code;
            if (event.value >= 127 && event.value <= 130)
                event.value = 128;
            js_event->value = (event.value * 65536) / 256 - 32768;
            if (keyabs[event.code] == js_event->value)
                return -1;
            keyabs[event.code] = js_event->value;
            break;
    }
    return 0;
}

void event_loop(int fd, int input, int is_joydev)
{
    char msg[BUF_SIZE];
    size_t len;
    ssize_t ret;
    int loop_on = 1;
    while (loop_on) {
        struct js_event js_event;

        if (is_joydev) {
            ret = read(input, &js_event, sizeof(struct js_event));
            if (ret > 0 && (size_t)ret >= sizeof(struct js_event)) {
                loop_on = handle_js_event(fd, js_event);
            }
        } else {
            struct input_event event;
            ret = read(input, &event, sizeof(struct input_event));
            if (ret > 0 && (size_t)ret >= sizeof(struct input_event)) {
                if (create_js_event(event, &js_event) == 0)
                    loop_on = handle_js_event(fd, js_event);
            }
        }
    }

    change_gear(fd, 0, 0);

    printf("disconnect\n");
    strncpy(msg, "disconnect", BUF_SIZE - 1);
    len = strnlen(msg, BUF_SIZE - 1);
    msg[len] = '\0';
    ret = write(fd, msg, len);
    if (ret < 0 || (size_t)ret != len) {
        fprintf(stderr, "partial/failed write\n");
    }
}

int main(int argc, char *argv[])
{
    int input;
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int sfd, s;
    int is_joydev = 0;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s host inputdev\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    input = open(argv[2], O_RDONLY);
    if (input < 0) {
        fprintf(stderr, "failed to open %s", argv[2]);
        exit(EXIT_FAILURE);
    }
    if (strncmp(argv[2], "/dev/input/js", strlen("/dev/input/js"))) {
        fprintf(stdout, "MODE: Input Subsystem\n");
        is_joydev = 0;
    } else {
        fprintf(stdout, "MODE: JoyStick\n");
    }

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

    /*
     * setup key map
     *
     * KEY      EV_ID       JS_ID
     * SQURE    BTN_A(304)  0
     * CROSS    BTN_B(305)  1
     * CIRCLE   BTN_C(306)  2
     * TRIAGNEL BTN_X(307)  3
     */
    keymap[BTN_A - BTN_MISC] = 0;
    keymap[BTN_B - BTN_MISC] = 1;
    keymap[BTN_C - BTN_MISC] = 2;
    keymap[BTN_X - BTN_MISC] = 3;

    event_loop(sfd, input, is_joydev);

    close(input);
    return 0;
}

