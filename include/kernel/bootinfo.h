#ifndef RIVERIX_BOOTINFO_H
#define RIVERIX_BOOTINFO_H

#include <stdint.h>

#include "kernel/multiboot.h"

typedef enum boot_root_policy {
    BOOT_ROOT_AUTO = 0,
    BOOT_ROOT_DISK = 1,
    BOOT_ROOT_RAMDISK = 2,
} boot_root_policy_t;

typedef struct boot_framebuffer_info {
    uint64_t physical_address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t type;
    uint8_t red_field_position;
    uint8_t red_mask_size;
    uint8_t green_field_position;
    uint8_t green_mask_size;
    uint8_t blue_field_position;
    uint8_t blue_mask_size;
    uint8_t present;
} boot_framebuffer_info_t;

#define BOOT_FLAG_RECOVERY (1u << 0)
#define BOOT_FLAG_REINSTALL (1u << 1)
#define BOOT_FLAG_SOAK (1u << 2)

void bootinfo_init(const multiboot_info_t *multiboot_info);
boot_root_policy_t bootinfo_root_policy(void);
const char *bootinfo_root_policy_name(boot_root_policy_t policy);
uint32_t bootinfo_flags(void);
const boot_framebuffer_info_t *bootinfo_framebuffer(void);
int bootinfo_recovery_enabled(void);
int bootinfo_reinstall_enabled(void);
int bootinfo_soak_enabled(void);

#endif
