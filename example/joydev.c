#include <stdio.h>
#include <stdlib.h>
#include <linux/input.h>
#include <unistd.h>

int main(void)
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

        if (read(input, &event, sizeof(event)) != sizeof(event)) {
            exit(EXIT_FAILURE);
        }
        switch (event.type) {
            case EV_KEY:
                if (event.code < BTN_MISC || event.value == 2)
                    break; /* ignore keyboard or auto repeat */
                js_event.type = JS_EVENT_BUTTON;
                js_event.number = joydev->keymap[event.code - BTN_MISC];
                js_event.value = event.value;
                break;

            case EV_ABS:
                js_event.type = JS_EVENT_AXIS;
                js_event.number = joydev->absmap[event.code];
                js_event.value = joydev_correct(event.value,
                        &joydev->corr[js_event.number]);
                if (js_event.value == joydev->abs[js_event.number])
                    return;
                joydev->abs[js_event.number] = event.value;
                break;
        }
    }
}
