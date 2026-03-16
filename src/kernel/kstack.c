#include "kernel/kstack.h"

#include <stdint.h>

#include "kernel/console.h"
#include "kernel/palloc.h"
#include "kernel/paging.h"

#define KSTACK_GUARD_PAGES 1u
#define KSTACK_DATA_PAGES 2u
#define KSTACK_SLOT_PAGES (KSTACK_GUARD_PAGES + KSTACK_DATA_PAGES)
#define KSTACK_SLOT_SIZE (KSTACK_SLOT_PAGES * PAGE_SIZE)
#define KSTACK_SLOT_COUNT ((KERNEL_STACK_LIMIT - KERNEL_STACK_BASE) / KSTACK_SLOT_SIZE)

static uint8_t kstack_slots[(KSTACK_SLOT_COUNT + 7u) / 8u];
static uint32_t kstack_ready;
static uint32_t kstack_active_count;

static void zero_bytes(void *buffer, uint32_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    uint32_t index;

    for (index = 0u; index < length; index++) {
        bytes[index] = 0u;
    }
}

static void kstack_clear(kernel_stack_t *stack) {
    if (stack == 0) {
        return;
    }

    stack->guard_base = 0u;
    stack->stack_base = 0u;
    stack->stack_top = 0u;
}

static uintptr_t kstack_slot_guard_base(uint32_t slot_index) {
    return KERNEL_STACK_BASE + ((uintptr_t)slot_index * KSTACK_SLOT_SIZE);
}

static uintptr_t kstack_slot_stack_base(uint32_t slot_index) {
    return kstack_slot_guard_base(slot_index) + (KSTACK_GUARD_PAGES * PAGE_SIZE);
}

static uintptr_t kstack_slot_stack_top(uint32_t slot_index) {
    return kstack_slot_stack_base(slot_index) + (KSTACK_DATA_PAGES * PAGE_SIZE);
}

static int kstack_slot_mark_used(uint32_t slot_index) {
    uint32_t byte_index = slot_index / 8u;
    uint8_t bit_mask = (uint8_t)(1u << (slot_index % 8u));

    if ((kstack_slots[byte_index] & bit_mask) != 0u) {
        return -1;
    }

    kstack_slots[byte_index] |= bit_mask;
    return 0;
}

static void kstack_slot_mark_free(uint32_t slot_index) {
    uint32_t byte_index = slot_index / 8u;
    uint8_t bit_mask = (uint8_t)(1u << (slot_index % 8u));

    kstack_slots[byte_index] &= (uint8_t)~bit_mask;
}

static int kstack_find_slot(uint32_t *out_slot_index) {
    uint32_t index;

    if (out_slot_index == 0) {
        return -1;
    }

    for (index = 0u; index < KSTACK_SLOT_COUNT; index++) {
        uint32_t byte_index = index / 8u;
        uint8_t bit_mask = (uint8_t)(1u << (index % 8u));

        if ((kstack_slots[byte_index] & bit_mask) != 0u) {
            continue;
        }

        *out_slot_index = index;
        return 0;
    }

    return -1;
}

static int kstack_slot_index_from_guard(uintptr_t guard_base, uint32_t *out_slot_index) {
    uintptr_t offset;

    if (out_slot_index == 0 || guard_base < KERNEL_STACK_BASE || guard_base >= KERNEL_STACK_LIMIT) {
        return -1;
    }

    offset = guard_base - KERNEL_STACK_BASE;
    if ((offset % KSTACK_SLOT_SIZE) != 0u) {
        return -1;
    }

    *out_slot_index = (uint32_t)(offset / KSTACK_SLOT_SIZE);
    if (*out_slot_index >= KSTACK_SLOT_COUNT) {
        return -1;
    }

    return 0;
}

void kstack_init(void) {
    zero_bytes(kstack_slots, sizeof(kstack_slots));
    kstack_ready = 1u;
    kstack_active_count = 0u;

    console_write("kstack: ready slots 0x");
    console_write_hex32(KSTACK_SLOT_COUNT);
    console_write(" pages 0x");
    console_write_hex32(KSTACK_DATA_PAGES);
    console_write(" guard 0x");
    console_write_hex32(KSTACK_GUARD_PAGES);
    console_write("\n");
}

int kstack_alloc(kernel_stack_t *stack) {
    uint32_t directory_phys;
    uint32_t slot_index;
    uint32_t physical_pages[KSTACK_DATA_PAGES];
    uint32_t index;

    if (kstack_ready == 0u || stack == 0) {
        return -1;
    }

    directory_phys = paging_current_directory_phys();
    kstack_clear(stack);

    if (kstack_find_slot(&slot_index) != 0 || kstack_slot_mark_used(slot_index) != 0) {
        return -1;
    }

    for (index = 0u; index < KSTACK_DATA_PAGES; index++) {
        physical_pages[index] = palloc_alloc_page();
        if (physical_pages[index] == 0u) {
            uint32_t rollback;

            for (rollback = 0u; rollback < index; rollback++) {
                palloc_free_page(physical_pages[rollback]);
            }

            kstack_slot_mark_free(slot_index);
            return -1;
        }

        zero_bytes((void *)paging_phys_to_virt(physical_pages[index]), PAGE_SIZE);
    }

    if (paging_map_pages_in(directory_phys, kstack_slot_stack_base(slot_index), physical_pages, KSTACK_DATA_PAGES, PAGE_WRITABLE) != 0) {
        for (index = 0u; index < KSTACK_DATA_PAGES; index++) {
            palloc_free_page(physical_pages[index]);
        }

        kstack_slot_mark_free(slot_index);
        return -1;
    }

    stack->guard_base = kstack_slot_guard_base(slot_index);
    stack->stack_base = kstack_slot_stack_base(slot_index);
    stack->stack_top = kstack_slot_stack_top(slot_index);
    kstack_active_count++;
    return 0;
}

void kstack_free(kernel_stack_t *stack) {
    uint32_t directory_phys;
    uint32_t slot_index;
    uint32_t index;

    if (stack == 0 || stack->guard_base == 0u) {
        return;
    }

    directory_phys = paging_current_directory_phys();
    if (kstack_slot_index_from_guard(stack->guard_base, &slot_index) != 0) {
        kstack_clear(stack);
        return;
    }

    for (index = 0u; index < KSTACK_DATA_PAGES; index++) {
        uintptr_t virtual_address = stack->stack_base + ((uintptr_t)index * PAGE_SIZE);
        uint32_t physical_page = paging_resolve_physical_in(directory_phys, virtual_address);

        if (physical_page != 0u) {
            paging_unmap_page_in(directory_phys, virtual_address);
            palloc_free_page(physical_page);
        }
    }

    paging_unmap_page_in(directory_phys, stack->guard_base);
    if (kstack_active_count != 0u) {
        kstack_active_count--;
    }

    kstack_slot_mark_free(slot_index);
    kstack_clear(stack);
}

int kstack_is_guard_address(uintptr_t virtual_address) {
    uintptr_t offset;

    if (virtual_address < KERNEL_STACK_BASE || virtual_address >= KERNEL_STACK_LIMIT) {
        return 0;
    }

    offset = virtual_address - KERNEL_STACK_BASE;
    return (offset % KSTACK_SLOT_SIZE) < (KSTACK_GUARD_PAGES * PAGE_SIZE) ? 1 : 0;
}

int kstack_selftest(void) {
    uint32_t directory_phys;
    kernel_stack_t stack;
    uint32_t probe_value = 0xA5A55A5Au;
    uintptr_t released_stack_base;
    uint32_t *stack_word;

    directory_phys = paging_current_directory_phys();
    kstack_clear(&stack);

    if (kstack_alloc(&stack) != 0) {
        console_write("kstack: selftest alloc failed\n");
        return -1;
    }

    if (paging_lookup_page_in(directory_phys, stack.guard_base, 0, 0) == 0) {
        console_write("kstack: selftest guard mapped\n");
        kstack_free(&stack);
        return -1;
    }

    if (!paging_range_present_in(directory_phys, stack.stack_base, KSTACK_DATA_PAGES, PAGE_WRITABLE)) {
        console_write("kstack: selftest stack missing\n");
        kstack_free(&stack);
        return -1;
    }

    stack_word = (uint32_t *)(stack.stack_top - sizeof(uint32_t));
    *stack_word = probe_value;
    if (*stack_word != probe_value) {
        console_write("kstack: selftest write failed\n");
        kstack_free(&stack);
        return -1;
    }

    released_stack_base = stack.stack_base;
    kstack_free(&stack);
    if (paging_lookup_page_in(directory_phys, released_stack_base, 0, 0) == 0) {
        console_write("kstack: selftest free failed\n");
        return -1;
    }

    console_write("kstack: selftest ok\n");
    return 0;
}
