#include "kernel/bootinfo.h"

#include <stdint.h>

#include "kernel/console.h"
#include "kernel/paging.h"
#include "kernel/trace.h"
#include "shared/syscall_abi.h"

static boot_root_policy_t root_policy_value = BOOT_ROOT_AUTO;
static uint32_t boot_flags_value = 0u;
static boot_framebuffer_info_t framebuffer_info_value;

_Static_assert((uint32_t)BOOT_ROOT_AUTO == SYS_BOOT_ROOT_AUTO, "boot root auto ABI mismatch");
_Static_assert((uint32_t)BOOT_ROOT_DISK == SYS_BOOT_ROOT_DISK, "boot root disk ABI mismatch");
_Static_assert((uint32_t)BOOT_ROOT_RAMDISK == SYS_BOOT_ROOT_RAMDISK, "boot root ramdisk ABI mismatch");
_Static_assert((uint32_t)BOOT_FLAG_RECOVERY == SYS_BOOT_FLAG_RECOVERY, "boot recovery flag ABI mismatch");
_Static_assert((uint32_t)BOOT_FLAG_REINSTALL == SYS_BOOT_FLAG_REINSTALL, "boot reinstall flag ABI mismatch");
_Static_assert((uint32_t)BOOT_FLAG_SOAK == SYS_BOOT_FLAG_SOAK, "boot soak flag ABI mismatch");

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
    framebuffer_info_value.present = 0u;
    framebuffer_info_value.physical_address = 0u;
    framebuffer_info_value.pitch = 0u;
    framebuffer_info_value.width = 0u;
    framebuffer_info_value.height = 0u;
    framebuffer_info_value.bpp = 0u;
    framebuffer_info_value.type = 0u;
    framebuffer_info_value.red_field_position = 0u;
    framebuffer_info_value.red_mask_size = 0u;
    framebuffer_info_value.green_field_position = 0u;
    framebuffer_info_value.green_mask_size = 0u;
    framebuffer_info_value.blue_field_position = 0u;
    framebuffer_info_value.blue_mask_size = 0u;

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

    if (string_contains(cmdline, "soak=1")) {
        boot_flags_value |= BOOT_FLAG_SOAK;
    }

    if (multiboot_info != 0 &&
        (multiboot_info->flags & MULTIBOOT_INFO_FRAMEBUFFER) != 0u &&
        multiboot_info->framebuffer_addr != 0u &&
        multiboot_info->framebuffer_pitch != 0u &&
        multiboot_info->framebuffer_width != 0u &&
        multiboot_info->framebuffer_height != 0u &&
        multiboot_info->framebuffer_bpp != 0u &&
        multiboot_info->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT) {
        framebuffer_info_value.present = 1u;
        framebuffer_info_value.physical_address = multiboot_info->framebuffer_addr;
        framebuffer_info_value.pitch = multiboot_info->framebuffer_pitch;
        framebuffer_info_value.width = multiboot_info->framebuffer_width;
        framebuffer_info_value.height = multiboot_info->framebuffer_height;
        framebuffer_info_value.bpp = multiboot_info->framebuffer_bpp;
        framebuffer_info_value.type = multiboot_info->framebuffer_type;
        framebuffer_info_value.red_field_position = multiboot_info->color_info.rgb.framebuffer_red_field_position;
        framebuffer_info_value.red_mask_size = multiboot_info->color_info.rgb.framebuffer_red_mask_size;
        framebuffer_info_value.green_field_position = multiboot_info->color_info.rgb.framebuffer_green_field_position;
        framebuffer_info_value.green_mask_size = multiboot_info->color_info.rgb.framebuffer_green_mask_size;
        framebuffer_info_value.blue_field_position = multiboot_info->color_info.rgb.framebuffer_blue_field_position;
        framebuffer_info_value.blue_mask_size = multiboot_info->color_info.rgb.framebuffer_blue_mask_size;
    }

    console_write("boot: root ");
    console_write(bootinfo_root_policy_name(root_policy_value));
    if ((boot_flags_value & BOOT_FLAG_RECOVERY) != 0u) {
        console_write(" recovery");
    }
    if ((boot_flags_value & BOOT_FLAG_REINSTALL) != 0u) {
        console_write(" reinstall");
    }
    if ((boot_flags_value & BOOT_FLAG_SOAK) != 0u) {
        console_write(" soak");
    }
    console_write("\n");

    trace_log(SYS_TRACE_CATEGORY_BOOT,
              SYS_TRACE_EVENT_BOOT_ROOT,
              (uint32_t)root_policy_value,
              boot_flags_value,
              0u);
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

const boot_framebuffer_info_t *bootinfo_framebuffer(void) {
    return &framebuffer_info_value;
}

int bootinfo_recovery_enabled(void) {
    return (boot_flags_value & BOOT_FLAG_RECOVERY) != 0u;
}

int bootinfo_reinstall_enabled(void) {
    return (boot_flags_value & BOOT_FLAG_REINSTALL) != 0u;
}

int bootinfo_soak_enabled(void) {
    return (boot_flags_value & BOOT_FLAG_SOAK) != 0u;
}
