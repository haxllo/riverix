#ifndef RIVERIX_BOOTINFO_H
#define RIVERIX_BOOTINFO_H

#include <stdint.h>

#include "kernel/multiboot.h"

typedef enum boot_root_policy {
    BOOT_ROOT_AUTO = 0,
    BOOT_ROOT_DISK = 1,
    BOOT_ROOT_RAMDISK = 2,
} boot_root_policy_t;

#define BOOT_FLAG_RECOVERY (1u << 0)

void bootinfo_init(const multiboot_info_t *multiboot_info);
boot_root_policy_t bootinfo_root_policy(void);
const char *bootinfo_root_policy_name(boot_root_policy_t policy);
uint32_t bootinfo_flags(void);
int bootinfo_recovery_enabled(void);

#endif
