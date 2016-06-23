#include <stdio.h>
#include <stdlib.h>
#include <linux/input.h>
#include <linux/joystick.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc, char **argv)
{
    int input;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s inputdev\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    input = open(argv[1], O_RDONLY);
    if (input < 0) {
        fprintf(stderr, "failed to open %s", argv[2]);
        exit(EXIT_FAILURE);
    }

    for (;;) {
        struct input_event event;
        struct js_event js_event;
        int old[6];

        if (read(input, &event, sizeof(event)) != sizeof(event)) {
            exit(EXIT_FAILURE);
        }
        switch (event.type) {
            case EV_KEY:
                if (event.code < BTN_MISC || event.value == 2)
                    break; /* ignore keyboard or auto repeat */
                /* circle: 306, cross: 305, squre: 304, triangle: 307 */
                /* press: 1, release: 0 */
                printf("BT: code=%d, value=%d\n", event.code, event.value);
                js_event.type = JS_EVENT_BUTTON;
                //js_event.number = joydev->keymap[event.code - BTN_MISC];
                js_event.value = event.value;
                break;

            case EV_ABS:
                /* left: 1, right: 5 (far 0, near 255) */
                js_event.type = JS_EVENT_AXIS;
                js_event.number = event.code;
                if (event.value >= 127 && event.value <= 130)
                    event.value = 128;
                js_event.value = (event.value * 65536) / 256 - 32768;
                if (old[event.code] == js_event.value)
                    break;
                if (event.code == 1)
                    printf("AX: code=%d, value=%d, %d\n", event.code, js_event.value, event.value);
                old[event.code] = js_event.value;
                break;
        }
    }
}
