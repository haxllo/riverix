#include "kernel/ramdisk.h"

#include <stdint.h>

#include "kernel/block.h"
#include "kernel/console.h"
#include "kernel/paging.h"
#include "shared/simplefs_format.h"

typedef struct ramdisk_context {
    const uint8_t *base;
    uint32_t byte_length;
} ramdisk_context_t;

static ramdisk_context_t rootfs_context;
static block_device_t rootfs_device;

static uint32_t string_length(const char *text) {
    uint32_t length = 0u;

    if (text == 0) {
        return 0u;
    }

    while (text[length] != '\0') {
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

static int32_t ramdisk_read(block_device_t *device, uint32_t block_index, uint32_t block_count, void *buffer) {
    ramdisk_context_t *context = (ramdisk_context_t *)device->context;
    uint8_t *destination = (uint8_t *)buffer;
    uint32_t offset = block_index * device->block_size;
    uint32_t length = block_count * device->block_size;
    uint32_t index;

    if (context == 0 || (offset + length) > context->byte_length) {
        return -1;
    }

    for (index = 0u; index < length; index++) {
        destination[index] = context->base[offset + index];
    }

    return 0;
}

int32_t ramdisk_register_rootfs(const multiboot_info_t *multiboot_info, const char *device_name) {
    const multiboot_module_t *module;
    const multiboot_module_t *modules;
    uint32_t module_count;
    uint32_t index;

    if (multiboot_info == 0 || device_name == 0) {
        return -1;
    }

    if ((multiboot_info->flags & MULTIBOOT_INFO_MODS) == 0u || multiboot_info->mods_count == 0u) {
        console_write("ramdisk: no multiboot modules\n");
        return -1;
    }

    modules = (const multiboot_module_t *)paging_phys_to_virt(multiboot_info->mods_addr);
    module = 0;
    module_count = multiboot_info->mods_count;

    for (index = 0u; index < module_count; index++) {
        const char *cmdline = 0;

        if (modules[index].cmdline != 0u) {
            cmdline = (const char *)paging_phys_to_virt(modules[index].cmdline);
        }

        console_write("ramdisk: module ");
        console_write_hex32(index);
        console_write(" cmdline ");
        if (cmdline != 0) {
            console_write(cmdline);
        } else {
            console_write("<none>");
        }
        console_write("\n");

        if (cmdline != 0 && string_contains(cmdline, "rootfs")) {
            module = &modules[index];
            break;
        }
    }

    if (module == 0) {
        module = &modules[0];
    }

    if (module->mod_end <= module->mod_start) {
        console_write("ramdisk: invalid module size\n");
        return -1;
    }

    rootfs_context.base = (const uint8_t *)paging_phys_to_virt(module->mod_start);
    rootfs_context.byte_length = module->mod_end - module->mod_start;

    if ((rootfs_context.byte_length % SIMPLEFS_BLOCK_SIZE) != 0u) {
        console_write("ramdisk: module not block aligned\n");
        return -1;
    }

    rootfs_device.name = device_name;
    rootfs_device.block_size = SIMPLEFS_BLOCK_SIZE;
    rootfs_device.block_count = rootfs_context.byte_length / SIMPLEFS_BLOCK_SIZE;
    rootfs_device.read_only = 1u;
    rootfs_device.read = ramdisk_read;
    rootfs_device.write = 0;
    rootfs_device.context = &rootfs_context;

    console_write("ramdisk: rootfs module bytes 0x");
    console_write_hex32(rootfs_context.byte_length);
    console_write("\n");

    return block_register(&rootfs_device);
}
