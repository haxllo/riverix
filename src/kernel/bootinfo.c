#include "kernel/bootinfo.h"

#include <stdint.h>

#include "kernel/console.h"
#include "kernel/paging.h"
#include "shared/syscall_abi.h"

static boot_root_policy_t root_policy_value = BOOT_ROOT_AUTO;
static uint32_t boot_flags_value = 0u;

_Static_assert((uint32_t)BOOT_ROOT_AUTO == SYS_BOOT_ROOT_AUTO, "boot root auto ABI mismatch");
_Static_assert((uint32_t)BOOT_ROOT_DISK == SYS_BOOT_ROOT_DISK, "boot root disk ABI mismatch");
_Static_assert((uint32_t)BOOT_ROOT_RAMDISK == SYS_BOOT_ROOT_RAMDISK, "boot root ramdisk ABI mismatch");
_Static_assert((uint32_t)BOOT_FLAG_RECOVERY == SYS_BOOT_FLAG_RECOVERY, "boot recovery flag ABI mismatch");
_Static_assert((uint32_t)BOOT_FLAG_REINSTALL == SYS_BOOT_FLAG_REINSTALL, "boot reinstall flag ABI mismatch");

static uint32_t string_length(const char *text) {
    uint32_t length = 0u;

    while (text != 0 && text[length] != '\0') {
        length++;
    }

    return length;
}

static int string_contains(const char *text, const char *pattern) {
    uint32_t text_length = string_length(text);
    uint32_t pattern_length = string_length(pattern);
    uint32_t start;
    uint32_t index;

    if (pattern == 0 || pattern_length == 0u) {
        return 1;
    }

    if (text == 0 || pattern_length > text_length) {
        return 0;
    }

    for (start = 0u; start <= (text_length - pattern_length); start++) {
        for (index = 0u; index < pattern_length; index++) {
            if (text[start + index] != pattern[index]) {
                break;
            }
        }

        if (index == pattern_length) {
            return 1;
        }
    }

    return 0;
}

void bootinfo_init(const multiboot_info_t *multiboot_info) {
    const char *cmdline = 0;

    root_policy_value = BOOT_ROOT_AUTO;
    boot_flags_value = 0u;

    if (multiboot_info != 0 &&
        (multiboot_info->flags & MULTIBOOT_INFO_CMDLINE) != 0u &&
        multiboot_info->cmdline != 0u) {
        cmdline = (const char *)paging_phys_to_virt(multiboot_info->cmdline);
    }

    if (string_contains(cmdline, "root=ramdisk")) {
        root_policy_value = BOOT_ROOT_RAMDISK;
    } else if (string_contains(cmdline, "root=disk")) {
        root_policy_value = BOOT_ROOT_DISK;
    }

    if (string_contains(cmdline, "recovery=1")) {
        boot_flags_value |= BOOT_FLAG_RECOVERY;
    }

    if (string_contains(cmdline, "reinstall=1")) {
        boot_flags_value |= BOOT_FLAG_REINSTALL;
    }

    console_write("boot: root ");
    console_write(bootinfo_root_policy_name(root_policy_value));
    if ((boot_flags_value & BOOT_FLAG_RECOVERY) != 0u) {
        console_write(" recovery");
    }
    if ((boot_flags_value & BOOT_FLAG_REINSTALL) != 0u) {
        console_write(" reinstall");
    }
    console_write("\n");
}

boot_root_policy_t bootinfo_root_policy(void) {
    return root_policy_value;
}

const char *bootinfo_root_policy_name(boot_root_policy_t policy) {
    switch (policy) {
    case BOOT_ROOT_DISK:
        return "disk";
    case BOOT_ROOT_RAMDISK:
        return "ramdisk";
    default:
        return "auto";
    }
}

uint32_t bootinfo_flags(void) {
    return boot_flags_value;
}

int bootinfo_recovery_enabled(void) {
    return (boot_flags_value & BOOT_FLAG_RECOVERY) != 0u;
}

int bootinfo_reinstall_enabled(void) {
    return (boot_flags_value & BOOT_FLAG_REINSTALL) != 0u;
}
