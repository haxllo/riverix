#include <stdint.h>

#include "user/boot.h"
#include "user/stdio.h"

static const char *root_policy_name(uint32_t root_policy) {
    switch (root_policy) {
    case BOOT_ROOT_DISK:
        return "disk";
    case BOOT_ROOT_RAMDISK:
        return "ramdisk";
    default:
        return "auto";
    }
}

int main(int argc, char **argv) {
    bootinfo_t info;

    (void)argc;
    (void)argv;

    if (getbootinfo(&info) != 0) {
        putstr_fd(2, "bootmode: unavailable\n");
        return 1;
    }

    putstr_fd(1, "bootmode: root ");
    putstr_fd(1, root_policy_name(info.root_policy));
    if ((info.flags & BOOT_FLAG_RECOVERY) != 0u) {
        putstr_fd(1, " recovery");
    }
    if ((info.flags & BOOT_FLAG_REINSTALL) != 0u) {
        putstr_fd(1, " reinstall");
    }
    putstr_fd(1, "\n");
    return 0;
}
