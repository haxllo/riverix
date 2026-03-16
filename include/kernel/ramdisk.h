#ifndef RIVERIX_RAMDISK_H
#define RIVERIX_RAMDISK_H

#include <stdint.h>

#include "kernel/multiboot.h"

int32_t ramdisk_register_rootfs(const multiboot_info_t *multiboot_info, const char *device_name);

#endif
