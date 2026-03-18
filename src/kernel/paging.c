#include "kernel/paging.h"

#include <stdint.h>

#include "kernel/console.h"
#include "kernel/palloc.h"

#define PAGE_DIRECTORY_ENTRIES 1024u
#define PAGE_TABLE_ENTRIES 1024u
#define PAGE_FLAGS (PAGE_PRESENT | PAGE_WRITABLE)
#define PAGE_TABLE_SPAN (PAGE_TABLE_ENTRIES * PAGE_SIZE)
#define RECURSIVE_DIRECTORY_INDEX 1023u
#define DIRECT_MAP_DIRECTORY_INDEX 768u
#define MMIO_DIRECTORY_INDEX_START (KERNEL_MMIO_BASE >> 22)
#define MMIO_DIRECTORY_INDEX_END (KERNEL_MMIO_LIMIT >> 22)
#define HEAP_DIRECTORY_INDEX_START (KERNEL_HEAP_BASE >> 22)
#define HEAP_DIRECTORY_INDEX_END (KERNEL_HEAP_LIMIT >> 22)
#define STACK_DIRECTORY_INDEX_START (KERNEL_STACK_BASE >> 22)
#define STACK_DIRECTORY_INDEX_END (KERNEL_STACK_LIMIT >> 22)
#define DIRECT_MAP_MAX_BYTES ((HEAP_DIRECTORY_INDEX_START - DIRECT_MAP_DIRECTORY_INDEX) * PAGE_TABLE_SPAN)
#define KERNEL_SHARED_LOW_DIRECTORY_COUNT 1u
#define PAGE_FRAME_MASK 0xFFFFF000u

static uint32_t kernel_page_directory_phys;
static uint32_t direct_map_limit;

static uint32_t align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uint32_t page_directory_index(uintptr_t virtual_address) {
    return (uint32_t)(virtual_address >> 22);
}

static uint32_t page_table_index(uintptr_t virtual_address) {
    return (uint32_t)((virtual_address >> 12) & 0x3FFu);
}

static uint32_t *paging_directory_from_phys(uint32_t directory_phys) {
    return (uint32_t *)paging_phys_to_virt(directory_phys);
}

static uint32_t *paging_table_from_entry(uint32_t entry) {
    return (uint32_t *)paging_phys_to_virt(entry & PAGE_FRAME_MASK);
}

static void copy_page_bytes(uint32_t destination_physical, uint32_t source_physical) {
    uint8_t *dst = (uint8_t *)paging_phys_to_virt(destination_physical);
    const uint8_t *src = (const uint8_t *)paging_phys_to_virt(source_physical);
    uint32_t index;

    for (index = 0u; index < PAGE_SIZE; index++) {
        dst[index] = src[index];
    }
}

static void zero_page(uint32_t physical_address) {
    uint32_t *words = (uint32_t *)(uintptr_t)physical_address;
    uint32_t index;

    for (index = 0; index < PAGE_SIZE / sizeof(uint32_t); index++) {
        words[index] = 0u;
    }
}

static void paging_invalidate_page(uintptr_t virtual_address) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
}

static void paging_load_directory(uint32_t physical_address) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(physical_address) : "memory");
}

static uint32_t paging_entry_flags(uint32_t flags) {
    return flags | PAGE_PRESENT;
}

static int paging_prepare_shared_tables(uint32_t *page_directory, uint32_t start_index, uint32_t end_index, const char *label) {
    uint32_t index;

    for (index = start_index; index < end_index; index++) {
        uint32_t table_phys;

        if ((page_directory[index] & PAGE_PRESENT) != 0u) {
            continue;
        }

        table_phys = palloc_alloc_page();
        if (table_phys == 0u) {
            console_write("paging: failed to allocate ");
            console_write(label);
            console_write(" table\n");
            return -1;
        }

        zero_page(table_phys);
        page_directory[index] = table_phys | PAGE_FLAGS;
    }

    return 0;
}

static void paging_enable_bit(void) {
    uint32_t cr0;

    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

void paging_init(void) {
    uint32_t *page_directory;
    uint32_t page_table_count;
    uint32_t table_index;

    direct_map_limit = align_up(palloc_managed_bytes(), PAGE_SIZE);
    if (direct_map_limit == 0u) {
        console_write("paging: no managed physical memory\n");
        return;
    }

    if (direct_map_limit > DIRECT_MAP_MAX_BYTES) {
        direct_map_limit = DIRECT_MAP_MAX_BYTES;
    }

    page_table_count = align_up(direct_map_limit, PAGE_TABLE_SPAN) / PAGE_TABLE_SPAN;
    kernel_page_directory_phys = palloc_alloc_page();
    if (kernel_page_directory_phys == 0u) {
        console_write("paging: failed to allocate page directory\n");
        return;
    }

    page_directory = (uint32_t *)(uintptr_t)kernel_page_directory_phys;

    zero_page(kernel_page_directory_phys);

    for (table_index = 0; table_index < page_table_count; table_index++) {
        uint32_t *page_table;
        uint32_t page_table_phys = palloc_alloc_page();
        uint32_t directory_offset = table_index * PAGE_TABLE_SPAN;
        uint32_t entry_index;

        if (page_table_phys == 0u) {
            console_write("paging: failed to allocate page table\n");
            return;
        }

        zero_page(page_table_phys);
        page_table = (uint32_t *)(uintptr_t)page_table_phys;

        for (entry_index = 0; entry_index < PAGE_TABLE_ENTRIES; entry_index++) {
            uint32_t physical_address = directory_offset + (entry_index * PAGE_SIZE);

            if (physical_address >= direct_map_limit) {
                break;
            }

            page_table[entry_index] = physical_address | PAGE_FLAGS;
        }

        page_directory[table_index] = page_table_phys | PAGE_FLAGS;
        page_directory[DIRECT_MAP_DIRECTORY_INDEX + table_index] = page_table_phys | PAGE_FLAGS;
    }

    if (paging_prepare_shared_tables(page_directory, HEAP_DIRECTORY_INDEX_START, HEAP_DIRECTORY_INDEX_END, "heap") != 0) {
        return;
    }

    if (paging_prepare_shared_tables(page_directory, MMIO_DIRECTORY_INDEX_START, MMIO_DIRECTORY_INDEX_END, "mmio") != 0) {
        return;
    }

    if (paging_prepare_shared_tables(page_directory, STACK_DIRECTORY_INDEX_START, STACK_DIRECTORY_INDEX_END, "stack") != 0) {
        return;
    }

    page_directory[RECURSIVE_DIRECTORY_INDEX] = kernel_page_directory_phys | PAGE_FLAGS;

    paging_load_directory(kernel_page_directory_phys);
    paging_enable_bit();

    console_write("paging: enabled pd=0x");
    console_write_hex32(kernel_page_directory_phys);
    console_write(" direct-map 0x");
    console_write_hex32(KERNEL_DIRECT_MAP_BASE);
    console_write("-0x");
    console_write_hex32(KERNEL_DIRECT_MAP_BASE + direct_map_limit);
    console_write("\n");
}

uint32_t paging_page_directory_phys(void) {
    return kernel_page_directory_phys;
}

uint32_t paging_current_directory_phys(void) {
    uint32_t directory_phys;

    __asm__ volatile ("mov %%cr3, %0" : "=r"(directory_phys));
    return directory_phys;
}

uint32_t paging_direct_map_limit(void) {
    return direct_map_limit;
}

uintptr_t paging_phys_to_virt(uint32_t physical_address) {
    return (uintptr_t)(KERNEL_DIRECT_MAP_BASE + physical_address);
}

uint32_t paging_virt_to_phys(uintptr_t virtual_address) {
    return (uint32_t)(virtual_address - KERNEL_DIRECT_MAP_BASE);
}

uint32_t paging_create_address_space(void) {
    uint32_t directory_phys;
    uint32_t *new_directory;
    uint32_t *kernel_directory;
    uint32_t index;

    directory_phys = palloc_alloc_page();
    if (directory_phys == 0u) {
        return 0u;
    }

    zero_page(directory_phys);
    new_directory = paging_directory_from_phys(directory_phys);
    kernel_directory = paging_directory_from_phys(kernel_page_directory_phys);

    for (index = 0u; index < KERNEL_SHARED_LOW_DIRECTORY_COUNT; index++) {
        new_directory[index] = kernel_directory[index];
    }

    for (index = DIRECT_MAP_DIRECTORY_INDEX; index < RECURSIVE_DIRECTORY_INDEX; index++) {
        new_directory[index] = kernel_directory[index];
    }

    new_directory[RECURSIVE_DIRECTORY_INDEX] = directory_phys | PAGE_FLAGS;
    return directory_phys;
}

void paging_destroy_address_space(uint32_t directory_phys) {
    uint32_t *directory;
    uint32_t *kernel_directory;
    uint32_t index;

    if (directory_phys == 0u || directory_phys == kernel_page_directory_phys) {
        return;
    }

    directory = paging_directory_from_phys(directory_phys);
    kernel_directory = paging_directory_from_phys(kernel_page_directory_phys);

    for (index = 0u; index < RECURSIVE_DIRECTORY_INDEX; index++) {
        if ((directory[index] & PAGE_PRESENT) == 0u) {
            continue;
        }

        if ((kernel_directory[index] & PAGE_FRAME_MASK) == (directory[index] & PAGE_FRAME_MASK)) {
            continue;
        }

        palloc_free_page(directory[index] & PAGE_FRAME_MASK);
        directory[index] = 0u;
    }

    palloc_free_page(directory_phys);
}

void paging_switch_directory(uint32_t directory_phys) {
    if (directory_phys == 0u) {
        return;
    }

    if (paging_current_directory_phys() == directory_phys) {
        return;
    }

    paging_load_directory(directory_phys);
}

uint32_t paging_resolve_physical_in(uint32_t directory_phys, uintptr_t virtual_address) {
    uint32_t directory_index;
    uint32_t table_index;
    uint32_t *page_directory;
    uint32_t *page_table;
    uint32_t page_entry;

    if (directory_phys == 0u) {
        return 0u;
    }

    directory_index = page_directory_index(virtual_address);
    table_index = page_table_index(virtual_address);
    page_directory = paging_directory_from_phys(directory_phys);

    if ((page_directory[directory_index] & PAGE_PRESENT) == 0u) {
        return 0u;
    }

    page_table = paging_table_from_entry(page_directory[directory_index]);
    page_entry = page_table[table_index];
    if ((page_entry & PAGE_PRESENT) == 0u) {
        return 0u;
    }

    return page_entry & PAGE_FRAME_MASK;
}

int paging_lookup_page_in(uint32_t directory_phys, uintptr_t virtual_address, uint32_t *out_physical_address, uint32_t *out_flags) {
    uint32_t directory_index;
    uint32_t table_index;
    uint32_t *page_directory;
    uint32_t *page_table;
    uint32_t page_entry;

    if (directory_phys == 0u) {
        return -1;
    }

    directory_index = page_directory_index(virtual_address);
    table_index = page_table_index(virtual_address);
    page_directory = paging_directory_from_phys(directory_phys);

    if ((page_directory[directory_index] & PAGE_PRESENT) == 0u) {
        return -1;
    }

    page_table = paging_table_from_entry(page_directory[directory_index]);
    page_entry = page_table[table_index];
    if ((page_entry & PAGE_PRESENT) == 0u) {
        return -1;
    }

    if (out_physical_address != 0) {
        *out_physical_address = page_entry & PAGE_FRAME_MASK;
    }

    if (out_flags != 0) {
        *out_flags = page_entry & ~PAGE_FRAME_MASK;
    }

    return 0;
}

int paging_map_page_in(uint32_t directory_phys, uintptr_t virtual_address, uint32_t physical_address, uint32_t flags) {
    uint32_t directory_index;
    uint32_t table_index;
    uint32_t *page_directory;
    uint32_t *page_table;

    if ((virtual_address & (PAGE_SIZE - 1u)) != 0u || (physical_address & (PAGE_SIZE - 1u)) != 0u) {
        return -1;
    }

    directory_index = page_directory_index(virtual_address);
    table_index = page_table_index(virtual_address);
    if (directory_phys == 0u) {
        return -1;
    }

    page_directory = paging_directory_from_phys(directory_phys);

    if ((page_directory[directory_index] & PAGE_PRESENT) == 0u) {
        uint32_t page_table_phys = palloc_alloc_page();
        uint32_t directory_flags = PAGE_PRESENT | PAGE_WRITABLE;

        if (page_table_phys == 0u) {
            return -1;
        }

        if ((flags & PAGE_USER) != 0u) {
            directory_flags |= PAGE_USER;
        }

        zero_page(page_table_phys);
        page_directory[directory_index] = page_table_phys | directory_flags;
    } else if ((flags & PAGE_USER) != 0u && (page_directory[directory_index] & PAGE_USER) == 0u) {
        if (directory_index < KERNEL_SHARED_LOW_DIRECTORY_COUNT || directory_index >= DIRECT_MAP_DIRECTORY_INDEX) {
            return -1;
        }

        page_directory[directory_index] |= PAGE_USER;
    }

    page_table = paging_table_from_entry(page_directory[directory_index]);
    page_table[table_index] = physical_address | paging_entry_flags(flags);

    if (paging_current_directory_phys() == directory_phys) {
        paging_invalidate_page(virtual_address);
    }

    return 0;
}

int paging_map_page(uintptr_t virtual_address, uint32_t physical_address, uint32_t flags) {
    return paging_map_page_in(paging_current_directory_phys(), virtual_address, physical_address, flags);
}

int paging_map_pages_in(uint32_t directory_phys, uintptr_t virtual_address, const uint32_t *physical_pages, uint32_t page_count, uint32_t flags) {
    uint32_t index;

    if (physical_pages == 0) {
        return -1;
    }

    for (index = 0u; index < page_count; index++) {
        if (paging_map_page_in(directory_phys, virtual_address + ((uintptr_t)index * PAGE_SIZE), physical_pages[index], flags) != 0) {
            paging_unmap_pages_in(directory_phys, virtual_address, index);
            return -1;
        }
    }

    return 0;
}

int paging_map_pages(uintptr_t virtual_address, const uint32_t *physical_pages, uint32_t page_count, uint32_t flags) {
    return paging_map_pages_in(paging_current_directory_phys(), virtual_address, physical_pages, page_count, flags);
}

void paging_unmap_page_in(uint32_t directory_phys, uintptr_t virtual_address) {
    uint32_t directory_index = page_directory_index(virtual_address);
    uint32_t table_index = page_table_index(virtual_address);
    uint32_t *page_directory;
    uint32_t *page_table;

    if (directory_phys == 0u || (virtual_address & (PAGE_SIZE - 1u)) != 0u) {
        return;
    }

    page_directory = paging_directory_from_phys(directory_phys);
    if ((page_directory[directory_index] & PAGE_PRESENT) == 0u) {
        return;
    }

    page_table = paging_table_from_entry(page_directory[directory_index]);
    page_table[table_index] = 0u;

    if (paging_current_directory_phys() == directory_phys) {
        paging_invalidate_page(virtual_address);
    }
}

void paging_unmap_page(uintptr_t virtual_address) {
    paging_unmap_page_in(paging_current_directory_phys(), virtual_address);
}

void paging_unmap_pages_in(uint32_t directory_phys, uintptr_t virtual_address, uint32_t page_count) {
    uint32_t index;

    for (index = 0u; index < page_count; index++) {
        paging_unmap_page_in(directory_phys, virtual_address + ((uintptr_t)index * PAGE_SIZE));
    }
}

void paging_unmap_pages(uintptr_t virtual_address, uint32_t page_count) {
    paging_unmap_pages_in(paging_current_directory_phys(), virtual_address, page_count);
}

int paging_range_present_in(uint32_t directory_phys, uintptr_t virtual_address, uint32_t page_count, uint32_t required_flags) {
    uint32_t index;

    for (index = 0u; index < page_count; index++) {
        uint32_t page_flags;

        if (paging_lookup_page_in(directory_phys, virtual_address + ((uintptr_t)index * PAGE_SIZE), 0, &page_flags) != 0) {
            return 0;
        }

        if ((page_flags & required_flags) != required_flags) {
            return 0;
        }
    }

    return 1;
}

int paging_user_range_mapped_in(uint32_t directory_phys, uintptr_t virtual_address, uint32_t length) {
    uintptr_t current_page;
    uintptr_t end_address;
    uint32_t *page_directory;

    if (directory_phys == 0u) {
        return 0;
    }

    page_directory = paging_directory_from_phys(directory_phys);

    if (length == 0u) {
        return 1;
    }

    end_address = virtual_address + (uintptr_t)length;
    if (end_address < virtual_address) {
        return 0;
    }

    if (virtual_address < USER_VIRT_BASE || end_address > USER_VIRT_LIMIT) {
        return 0;
    }

    current_page = virtual_address & ~(uintptr_t)(PAGE_SIZE - 1u);
    while (current_page < end_address) {
        uint32_t directory_index = page_directory_index(current_page);
        uint32_t table_index = page_table_index(current_page);
        uint32_t *page_table;

        if ((page_directory[directory_index] & (PAGE_PRESENT | PAGE_USER)) != (PAGE_PRESENT | PAGE_USER)) {
            return 0;
        }

        page_table = paging_table_from_entry(page_directory[directory_index]);
        if ((page_table[table_index] & (PAGE_PRESENT | PAGE_USER)) != (PAGE_PRESENT | PAGE_USER)) {
            return 0;
        }

        current_page += PAGE_SIZE;
    }

    return 1;
}

int paging_user_range_mapped(uintptr_t virtual_address, uint32_t length) {
    return paging_user_range_mapped_in(paging_current_directory_phys(), virtual_address, length);
}

int paging_user_range_writable_in(uint32_t directory_phys, uintptr_t virtual_address, uint32_t length) {
    uintptr_t current_page;
    uintptr_t end_address;

    if (directory_phys == 0u) {
        return 0;
    }

    if (length == 0u) {
        return 1;
    }

    end_address = virtual_address + (uintptr_t)length;
    if (end_address < virtual_address) {
        return 0;
    }

    if (virtual_address < USER_VIRT_BASE || end_address > USER_VIRT_LIMIT) {
        return 0;
    }

    current_page = virtual_address & ~(uintptr_t)(PAGE_SIZE - 1u);
    while (current_page < end_address) {
        uint32_t page_flags;

        if (paging_lookup_page_in(directory_phys, current_page, 0, &page_flags) != 0 ||
            (page_flags & PAGE_USER) == 0u ||
            (page_flags & (PAGE_WRITABLE | PAGE_COPY_ON_WRITE)) == 0u) {
            return 0;
        }

        current_page += PAGE_SIZE;
    }

    return 1;
}

int paging_user_range_writable(uintptr_t virtual_address, uint32_t length) {
    return paging_user_range_writable_in(paging_current_directory_phys(), virtual_address, length);
}

int paging_resolve_copy_on_write_in(uint32_t directory_phys, uintptr_t virtual_address) {
    uintptr_t page_virtual = virtual_address & ~(uintptr_t)(PAGE_SIZE - 1u);
    uint32_t physical_page;
    uint32_t page_flags;
    uint32_t writable_flags;
    uint32_t refcount;

    if (paging_lookup_page_in(directory_phys, page_virtual, &physical_page, &page_flags) != 0 ||
        (page_flags & (PAGE_PRESENT | PAGE_USER | PAGE_COPY_ON_WRITE)) != (PAGE_PRESENT | PAGE_USER | PAGE_COPY_ON_WRITE) ||
        (page_flags & PAGE_WRITABLE) != 0u) {
        return -1;
    }

    writable_flags = (page_flags | PAGE_WRITABLE) & ~PAGE_COPY_ON_WRITE;
    refcount = palloc_page_refcount(physical_page);
    if (refcount == 0u) {
        return -1;
    }

    if (refcount == 1u) {
        if (paging_map_page_in(directory_phys, page_virtual, physical_page, writable_flags) != 0) {
            return -1;
        }

        console_write("paging: cow promote va 0x");
        console_write_hex32((uint32_t)page_virtual);
        console_write("\n");
        return 0;
    }

    {
        uint32_t new_page = palloc_alloc_page();

        if (new_page == 0u) {
            return -1;
        }

        copy_page_bytes(new_page, physical_page);
        if (paging_map_page_in(directory_phys, page_virtual, new_page, writable_flags) != 0) {
            palloc_free_page(new_page);
            return -1;
        }

        palloc_free_page(physical_page);

        console_write("paging: cow split va 0x");
        console_write_hex32((uint32_t)page_virtual);
        console_write(" old 0x");
        console_write_hex32(physical_page);
        console_write(" new 0x");
        console_write_hex32(new_page);
        console_write("\n");
    }

    return 0;
}
