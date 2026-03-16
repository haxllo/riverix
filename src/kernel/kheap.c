#include "kernel/kheap.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/console.h"
#include "kernel/palloc.h"
#include "kernel/paging.h"

#define KHEAP_ALIGN 16u
#define KHEAP_INITIAL_PAGES 4u
#define KHEAP_MIN_SPLIT (sizeof(kheap_block_t) + KHEAP_ALIGN)

typedef struct kheap_block {
    uint32_t size;
    uint32_t free;
    struct kheap_block *next;
    struct kheap_block *prev;
} kheap_block_t;

static kheap_block_t *heap_head;
static uintptr_t heap_end;
static uint32_t committed_pages;
static uint32_t kheap_ready;

static size_t align_up_size(size_t value, size_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static void zero_bytes(void *buffer, size_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    size_t index;

    for (index = 0u; index < length; index++) {
        bytes[index] = 0u;
    }
}

static kheap_block_t *kheap_last_block(void) {
    kheap_block_t *block = heap_head;

    if (block == 0) {
        return 0;
    }

    while (block->next != 0) {
        block = block->next;
    }

    return block;
}

static void kheap_split_block(kheap_block_t *block, size_t size) {
    kheap_block_t *tail;
    uintptr_t tail_address;

    if (block == 0 || block->size < (size + KHEAP_MIN_SPLIT)) {
        return;
    }

    tail_address = (uintptr_t)block + sizeof(*block) + size;
    tail = (kheap_block_t *)tail_address;
    tail->size = block->size - (uint32_t)size - sizeof(*block);
    tail->free = 1u;
    tail->next = block->next;
    tail->prev = block;
    if (tail->next != 0) {
        tail->next->prev = tail;
    }

    block->size = (uint32_t)size;
    block->next = tail;
}

static void kheap_coalesce_forward(kheap_block_t *block) {
    kheap_block_t *next;

    if (block == 0 || block->free == 0u) {
        return;
    }

    next = block->next;
    if (next == 0 || next->free == 0u) {
        return;
    }

    block->size += sizeof(*block) + next->size;
    block->next = next->next;
    if (block->next != 0) {
        block->next->prev = block;
    }
}

static void kheap_coalesce(kheap_block_t *block) {
    if (block == 0) {
        return;
    }

    if (block->prev != 0 && block->prev->free != 0u) {
        block = block->prev;
        kheap_coalesce_forward(block);
    }

    kheap_coalesce_forward(block);
}

static int kheap_expand(size_t minimum_payload) {
    size_t minimum_bytes = align_up_size(minimum_payload + sizeof(kheap_block_t), PAGE_SIZE);
    uint32_t pages = (uint32_t)(minimum_bytes / PAGE_SIZE);
    uintptr_t expansion_base = heap_end;
    uint32_t mapped = 0u;
    kheap_block_t *block;
    kheap_block_t *tail;

    if (pages < KHEAP_INITIAL_PAGES) {
        pages = KHEAP_INITIAL_PAGES;
    }

    if ((heap_end + ((uintptr_t)pages * PAGE_SIZE)) > KERNEL_HEAP_LIMIT) {
        return -1;
    }

    while (mapped < pages) {
        uint32_t physical_page = palloc_alloc_page();

        if (physical_page == 0u || paging_map_page(heap_end, physical_page, PAGE_WRITABLE) != 0) {
            while (mapped != 0u) {
                uint32_t mapped_page_phys;

                heap_end -= PAGE_SIZE;
                mapped_page_phys = paging_resolve_physical_in(paging_current_directory_phys(), heap_end);
                if (mapped_page_phys != 0u) {
                    paging_unmap_page(heap_end);
                    palloc_free_page(mapped_page_phys);
                }
                mapped--;
            }

            heap_end = expansion_base;
            return -1;
        }

        zero_bytes((void *)heap_end, PAGE_SIZE);
        heap_end += PAGE_SIZE;
        mapped++;
        committed_pages++;
    }

    block = (kheap_block_t *)expansion_base;
    block->size = (pages * PAGE_SIZE) - sizeof(*block);
    block->free = 1u;
    block->next = 0;
    block->prev = 0;

    tail = kheap_last_block();
    if (tail == 0) {
        heap_head = block;
        return 0;
    }

    tail->next = block;
    block->prev = tail;
    kheap_coalesce(block);
    return 0;
}

void kheap_init(void) {
    heap_head = 0;
    heap_end = KERNEL_HEAP_BASE;
    committed_pages = 0u;
    kheap_ready = 0u;

    if (kheap_expand(PAGE_SIZE) != 0) {
        console_write("kheap: init failed\n");
        return;
    }

    kheap_ready = 1u;

    console_write("kheap: ready base 0x");
    console_write_hex32((uint32_t)KERNEL_HEAP_BASE);
    console_write(" limit 0x");
    console_write_hex32((uint32_t)KERNEL_HEAP_LIMIT);
    console_write(" pages 0x");
    console_write_hex32(committed_pages);
    console_write("\n");
}

void *kmalloc(size_t size) {
    kheap_block_t *block;
    size_t aligned_size;

    if (kheap_ready == 0u || size == 0u) {
        return 0;
    }

    aligned_size = align_up_size(size, KHEAP_ALIGN);

    for (;;) {
        for (block = heap_head; block != 0; block = block->next) {
            if (block->free == 0u || block->size < aligned_size) {
                continue;
            }

            kheap_split_block(block, aligned_size);
            block->free = 0u;
            return (void *)((uintptr_t)block + sizeof(*block));
        }

        if (kheap_expand(aligned_size) != 0) {
            return 0;
        }
    }
}

void *kzalloc(size_t size) {
    void *buffer = kmalloc(size);

    if (buffer != 0) {
        zero_bytes(buffer, size);
    }

    return buffer;
}

void kfree(void *pointer) {
    kheap_block_t *block;

    if (pointer == 0) {
        return;
    }

    block = (kheap_block_t *)((uintptr_t)pointer - sizeof(kheap_block_t));
    block->free = 1u;
    kheap_coalesce(block);
}

int kheap_selftest(void) {
    uint8_t *first;
    uint8_t *second;
    uint8_t *third;
    uint32_t index;

    first = (uint8_t *)kmalloc(24u);
    second = (uint8_t *)kzalloc(64u);
    if (first == 0 || second == 0) {
        console_write("kheap: selftest alloc failed\n");
        kfree(first);
        kfree(second);
        return -1;
    }

    for (index = 0u; index < 24u; index++) {
        first[index] = (uint8_t)(index + 1u);
    }

    for (index = 0u; index < 64u; index++) {
        if (second[index] != 0u) {
            console_write("kheap: selftest zero failed\n");
            kfree(first);
            kfree(second);
            return -1;
        }
    }

    kfree(first);
    third = (uint8_t *)kmalloc(16u);
    if (third == 0) {
        console_write("kheap: selftest reuse failed\n");
        kfree(second);
        return -1;
    }

    kfree(third);
    kfree(second);

    console_write("kheap: selftest ok\n");
    return 0;
}

uint32_t kheap_committed_pages(void) {
    return committed_pages;
}
