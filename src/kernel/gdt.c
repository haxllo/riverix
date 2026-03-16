#include "kernel/gdt.h"

#include <stdint.h>

#include "kernel/console.h"

typedef struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct gdt_descriptor {
    uint16_t size;
    uint32_t offset;
} __attribute__((packed)) gdt_descriptor_t;

typedef struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed)) tss_entry_t;

static gdt_entry_t gdt[6];
static gdt_descriptor_t gdt_descriptor;
static tss_entry_t tss;

extern void gdt_load(uint32_t descriptor_address);
extern void tss_load(uint16_t selector);

static void gdt_set_entry(uint32_t index, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity) {
    gdt[index].limit_low = (uint16_t)(limit & 0xFFFFu);
    gdt[index].base_low = (uint16_t)(base & 0xFFFFu);
    gdt[index].base_middle = (uint8_t)((base >> 16) & 0xFFu);
    gdt[index].access = access;
    gdt[index].granularity = (uint8_t)(((limit >> 16) & 0x0Fu) | (granularity & 0xF0u));
    gdt[index].base_high = (uint8_t)((base >> 24) & 0xFFu);
}

static void tss_init(void) {
    uint8_t *bytes = (uint8_t *)(uintptr_t)&tss;
    uint32_t index;

    for (index = 0u; index < sizeof(tss); index++) {
        bytes[index] = 0u;
    }

    tss.ss0 = GDT_KERNEL_DATA_SELECTOR;
    tss.iomap_base = (uint16_t)sizeof(tss);
}

void gdt_init(void) {
    gdt_set_entry(0u, 0u, 0u, 0u, 0u);
    gdt_set_entry(1u, 0u, 0x000FFFFFu, 0x9Au, 0xCFu);
    gdt_set_entry(2u, 0u, 0x000FFFFFu, 0x92u, 0xCFu);
    gdt_set_entry(3u, 0u, 0x000FFFFFu, 0xFAu, 0xCFu);
    gdt_set_entry(4u, 0u, 0x000FFFFFu, 0xF2u, 0xCFu);

    tss_init();
    gdt_set_entry(5u, (uint32_t)(uintptr_t)&tss, (uint32_t)(sizeof(tss) - 1u), 0x89u, 0x40u);

    gdt_descriptor.size = (uint16_t)(sizeof(gdt) - 1u);
    gdt_descriptor.offset = (uint32_t)(uintptr_t)&gdt[0];

    gdt_load((uint32_t)(uintptr_t)&gdt_descriptor);
    tss_load((uint16_t)GDT_TSS_SELECTOR);

    console_write("gdt: loaded\n");
}

void gdt_set_kernel_stack(uint32_t stack_top) {
    tss.esp0 = stack_top;
}
