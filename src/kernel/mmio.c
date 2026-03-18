#include "kernel/mmio.h"

#include <stdint.h>

#include "kernel/console.h"
#include "kernel/paging.h"
#include "kernel/palloc.h"

static uintptr_t mmio_next_virtual = KERNEL_MMIO_BASE;

static uint32_t align_down(uint32_t value, uint32_t alignment) {
    return value & ~(alignment - 1u);
}

static uint32_t align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

void mmio_init(void) {
    mmio_next_virtual = KERNEL_MMIO_BASE;
}

uintptr_t mmio_map_region(uint32_t physical_address, uint32_t length) {
    uint32_t physical_base;
    uint32_t physical_end;
    uint32_t map_length;
    uintptr_t virtual_base;
    uint32_t offset;

    if (length == 0u) {
        return 0u;
    }

    physical_base = align_down(physical_address, PAGE_SIZE);
    physical_end = align_up(physical_address + length, PAGE_SIZE);
    if (physical_end < physical_base) {
        return 0u;
    }

    map_length = physical_end - physical_base;
    if ((mmio_next_virtual + map_length) > KERNEL_MMIO_LIMIT) {
        console_write("mmio: exhausted window\n");
        return 0u;
    }

    virtual_base = mmio_next_virtual;
    for (offset = 0u; offset < map_length; offset += PAGE_SIZE) {
        if (paging_map_page(virtual_base + offset, physical_base + offset, PAGE_WRITABLE | PAGE_CACHE_DISABLE) != 0) {
            console_write("mmio: map failed\n");
            return 0u;
        }
    }

    mmio_next_virtual += map_length;

    console_write("mmio: mapped phys 0x");
    console_write_hex32(physical_base);
    console_write(" virt 0x");
    console_write_hex32((uint32_t)virtual_base);
    console_write(" bytes 0x");
    console_write_hex32(map_length);
    console_write("\n");

    return virtual_base + (physical_address - physical_base);
}
