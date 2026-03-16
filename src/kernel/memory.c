#include "kernel/memory.h"

#include <stdint.h>

#include "kernel/console.h"

static const char *memory_type_name(uint32_t type) {
    switch (type) {
    case MULTIBOOT_MEMORY_AVAILABLE:
        return "usable";
    case MULTIBOOT_MEMORY_RESERVED:
        return "reserved";
    case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE:
        return "acpi";
    case MULTIBOOT_MEMORY_NVS:
        return "nvs";
    case MULTIBOOT_MEMORY_BADRAM:
        return "badram";
    default:
        return "unknown";
    }
}

void memory_report(const multiboot_info_t *multiboot_info) {
    const multiboot_memory_map_t *entry;

    if ((multiboot_info->flags & MULTIBOOT_INFO_MMAP) == 0u) {
        console_write("memory: no multiboot memory map, using legacy totals only\n");

        if ((multiboot_info->flags & MULTIBOOT_INFO_MEMORY) != 0u) {
            console_write("memory: mem_lower 0x");
            console_write_hex32(multiboot_info->mem_lower);
            console_write(" KiB, mem_upper 0x");
            console_write_hex32(multiboot_info->mem_upper);
            console_write(" KiB\n");
        }

        return;
    }

    entry = multiboot_mmap_begin(multiboot_info);
    while (entry < multiboot_mmap_end(multiboot_info)) {
        uint64_t end = entry->addr + entry->len;

        console_write("memory: region 0x");
        console_write_hex64(entry->addr);
        console_write("-0x");
        console_write_hex64(end);
        console_write(" ");
        console_write(memory_type_name(entry->type));
        console_write("\n");

        entry = multiboot_mmap_next(entry);
    }

    console_write("memory: map complete\n");
}
