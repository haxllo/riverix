#ifndef RIVERIX_MULTIBOOT_H
#define RIVERIX_MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002u

#define MULTIBOOT_INFO_MEMORY (1u << 0)
#define MULTIBOOT_INFO_CMDLINE (1u << 2)
#define MULTIBOOT_INFO_MODS (1u << 3)
#define MULTIBOOT_INFO_MMAP (1u << 6)
#define MULTIBOOT_INFO_FRAMEBUFFER (1u << 12)

#define MULTIBOOT_MEMORY_AVAILABLE 1u
#define MULTIBOOT_MEMORY_RESERVED 2u
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3u
#define MULTIBOOT_MEMORY_NVS 4u
#define MULTIBOOT_MEMORY_BADRAM 5u

#define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED 0u
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB 1u
#define MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT 2u

typedef struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    union {
        struct {
            uint32_t framebuffer_palette_addr;
            uint16_t framebuffer_palette_num_colors;
        } __attribute__((packed)) palette;
        struct {
            uint8_t framebuffer_red_field_position;
            uint8_t framebuffer_red_mask_size;
            uint8_t framebuffer_green_field_position;
            uint8_t framebuffer_green_mask_size;
            uint8_t framebuffer_blue_field_position;
            uint8_t framebuffer_blue_mask_size;
        } __attribute__((packed)) rgb;
    } __attribute__((packed)) color_info;
} __attribute__((packed)) multiboot_info_t;

typedef struct multiboot_memory_map {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed)) multiboot_memory_map_t;

typedef struct multiboot_module {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t pad;
} multiboot_module_t;

static inline const multiboot_memory_map_t *multiboot_mmap_begin(const multiboot_info_t *info) {
    return (const multiboot_memory_map_t *)(uintptr_t)info->mmap_addr;
}

static inline const multiboot_memory_map_t *multiboot_mmap_end(const multiboot_info_t *info) {
    return (const multiboot_memory_map_t *)((uintptr_t)info->mmap_addr + info->mmap_length);
}

static inline const multiboot_memory_map_t *multiboot_mmap_next(const multiboot_memory_map_t *entry) {
    return (const multiboot_memory_map_t *)((uintptr_t)entry + entry->size + sizeof(entry->size));
}

static inline const multiboot_module_t *multiboot_module_begin(const multiboot_info_t *info) {
    return (const multiboot_module_t *)(uintptr_t)info->mods_addr;
}

static inline const multiboot_module_t *multiboot_module_end(const multiboot_info_t *info) {
    return multiboot_module_begin(info) + info->mods_count;
}

#endif
