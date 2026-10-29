/* Register an OS controller for packaged-player tests; EOF removes only this device. */
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static const int axes[] = {ABS_X, ABS_Y, ABS_Z, ABS_RZ, ABS_GAS, ABS_BRAKE, ABS_HAT0X, ABS_HAT0Y};
static const int buttons[] = {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR,
    BTN_SELECT, BTN_START, BTN_THUMBL, BTN_THUMBR};

static int contains(const int* values, size_t count, int value) {
    for (size_t i = 0; i < count; ++i) if (values[i] == value) return 1;
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 2 || (strcmp(argv[1], "A") && strcmp(argv[1], "B"))) {
        fputs("Expected controller label A or B\n", stderr);
        return 2;
    }
    int fd = open("/dev/uinput", O_WRONLY | O_CLOEXEC);
    if (fd < 0) { perror("open uinput"); return 1; }
    int created = 0, result = 1;
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(fd, UI_SET_EVBIT, EV_ABS) < 0) goto failed;
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
        if (ioctl(fd, UI_SET_KEYBIT, buttons[i]) < 0) goto failed;
    for (size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); ++i)
        if (ioctl(fd, UI_SET_ABSBIT, axes[i]) < 0) goto failed;
    char physical[64];
    snprintf(physical, sizeof(physical), "engine-controller-test-%s", argv[1]);
    if (ioctl(fd, UI_SET_PHYS, physical) < 0) goto failed;
    struct uinput_user_dev device;
    memset(&device, 0, sizeof(device));
    snprintf(device.name, sizeof(device.name), "Engine Controller Test %s", argv[1]);
    device.id.bustype = BUS_USB;
    device.id.vendor = 1;
    device.id.product = argv[1][0] == 'A' ? 1 : 2;
    device.id.version = 1;
    for (size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); ++i) {
        int axis = axes[i];
        int hat = axis == ABS_HAT0X || axis == ABS_HAT0Y;
        int stick = axis == ABS_X || axis == ABS_Y || axis == ABS_Z || axis == ABS_RZ;
        device.absmin[axis] = stick ? -32768 : hat ? -1 : 0;
        device.absmax[axis] = stick ? 32767 : hat ? 1 : 255;
        device.absflat[axis] = stick ? 4096 : 0;
    }
    if (write(fd, &device, sizeof(device)) != (ssize_t)sizeof(device) || ioctl(fd, UI_DEV_CREATE) < 0) goto failed;
    created = 1;
    puts("ready");
    fflush(stdout);
    char line[128];
    while (fgets(line, sizeof(line), stdin)) {
        int type, code, value;
        char extra;
        if (sscanf(line, "%d %d %d %c", &type, &code, &value, &extra) != 3 ||
            !((type == EV_SYN && code == SYN_REPORT && value == 0) ||
              (type == EV_KEY && contains(buttons, sizeof(buttons) / sizeof(buttons[0]), code) &&
               (value == 0 || value == 1)) ||
              (type == EV_ABS && contains(axes, sizeof(axes) / sizeof(axes[0]), code) &&
               value >= device.absmin[code] && value <= device.absmax[code]))) {
            fputs("Invalid controller event\n", stderr);
            goto cleanup;
        }
        struct input_event event;
        memset(&event, 0, sizeof(event));
        event.type = (unsigned short)type;
        event.code = (unsigned short)code;
        event.value = value;
        if (write(fd, &event, sizeof(event)) != (ssize_t)sizeof(event)) goto failed;
    }
    if (ferror(stdin)) goto failed;
    result = 0;
    goto cleanup;
failed:
    perror("controller device");
cleanup:
    if (created && ioctl(fd, UI_DEV_DESTROY) < 0) { perror("destroy controller"); result = 1; }
    close(fd);
    return result;
}
