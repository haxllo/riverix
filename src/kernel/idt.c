#include "kernel/idt.h"

#include <stdint.h>

#include "kernel/console.h"
#include "kernel/gdt.h"
#include "kernel/kstack.h"
#include "kernel/paging.h"
#include "kernel/pic.h"
#include "kernel/pit.h"
#include "kernel/proc.h"
#include "kernel/syscall.h"

typedef struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attributes;
    uint16_t offset_high;
} __attribute__((packed)) idt_entry_t;

typedef struct idt_descriptor {
    uint16_t size;
    uint32_t offset;
} __attribute__((packed)) idt_descriptor_t;

static idt_entry_t idt[256];
static idt_descriptor_t idt_descriptor;

extern void idt_load(uint32_t descriptor_address);
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);
extern void isr128(void);
extern void irq0(void);

static void idt_set_gate(uint8_t vector, uintptr_t handler, uint16_t selector, uint8_t type_attributes) {
    idt[vector].offset_low = (uint16_t)(handler & 0xFFFFu);
    idt[vector].selector = selector;
    idt[vector].zero = 0u;
    idt[vector].type_attributes = type_attributes;
    idt[vector].offset_high = (uint16_t)((handler >> 16) & 0xFFFFu);
}

static void print_page_fault_access(uint32_t error_code) {
    console_write((error_code & 0x1u) != 0u ? " prot" : " not-present");
    console_write((error_code & 0x2u) != 0u ? " write" : " read");
    console_write((error_code & 0x4u) != 0u ? " user" : " kernel");

    if ((error_code & 0x8u) != 0u) {
        console_write(" reserved");
    }

    if ((error_code & 0x10u) != 0u) {
        console_write(" instr");
    }
}

static void print_fault_details(const interrupt_frame_t *frame) {
    console_write("trap:");
    console_write(interrupt_from_user(frame) ? " user" : " kernel");

    if (proc_current_pid() != 0u) {
        console_write(" pid 0x");
        console_write_hex32(proc_current_pid());
    }

    console_write(" vector 0x");
    console_write_hex32(frame->int_no);
    console_write(" err 0x");
    console_write_hex32(frame->err_code);
    console_write(" eip 0x");
    console_write_hex32(frame->eip);
    console_write(" cs 0x");
    console_write_hex32(frame->cs);
    console_write(" eflags 0x");
    console_write_hex32(frame->eflags);

    if (frame->int_no == 14u) {
        uint32_t cr2;

        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        console_write(" cr2 0x");
        console_write_hex32(cr2);
        print_page_fault_access(frame->err_code);
    }

    console_write("\n");
}

void idt_init(void) {
    uint32_t index;
    uintptr_t exception_handlers[32] = {
        (uintptr_t)isr0,  (uintptr_t)isr1,  (uintptr_t)isr2,  (uintptr_t)isr3,
        (uintptr_t)isr4,  (uintptr_t)isr5,  (uintptr_t)isr6,  (uintptr_t)isr7,
        (uintptr_t)isr8,  (uintptr_t)isr9,  (uintptr_t)isr10, (uintptr_t)isr11,
        (uintptr_t)isr12, (uintptr_t)isr13, (uintptr_t)isr14, (uintptr_t)isr15,
        (uintptr_t)isr16, (uintptr_t)isr17, (uintptr_t)isr18, (uintptr_t)isr19,
        (uintptr_t)isr20, (uintptr_t)isr21, (uintptr_t)isr22, (uintptr_t)isr23,
        (uintptr_t)isr24, (uintptr_t)isr25, (uintptr_t)isr26, (uintptr_t)isr27,
        (uintptr_t)isr28, (uintptr_t)isr29, (uintptr_t)isr30, (uintptr_t)isr31
    };

    for (index = 0u; index < 256u; index++) {
        idt_set_gate((uint8_t)index, 0u, 0x08u, 0u);
    }

    for (index = 0u; index < 32u; index++) {
        idt_set_gate((uint8_t)index, exception_handlers[index], GDT_KERNEL_CODE_SELECTOR, 0x8Eu);
    }

    idt_set_gate(32u, (uintptr_t)irq0, GDT_KERNEL_CODE_SELECTOR, 0x8Eu);
    idt_set_gate(128u, (uintptr_t)isr128, GDT_KERNEL_CODE_SELECTOR, 0xEEu);

    idt_descriptor.size = (uint16_t)(sizeof(idt) - 1u);
    idt_descriptor.offset = (uint32_t)(uintptr_t)&idt[0];

    idt_load((uint32_t)(uintptr_t)&idt_descriptor);

    console_write("idt: loaded\n");
}

uint32_t interrupt_dispatch(interrupt_frame_t *frame) {
    if (frame->int_no == 32u) {
        uint32_t next_stack;

        pit_handle_tick();
        if (!proc_preemptible_from_interrupt(frame)) {
            pic_send_eoi(0u);
            return (uint32_t)(uintptr_t)frame;
        }
        next_stack = proc_schedule(frame);
        pic_send_eoi(0u);
        return next_stack;
    }

    if (frame->int_no == 128u) {
        return syscall_dispatch(frame);
    }

    if (frame->int_no == 14u && interrupt_from_user(frame) && (frame->err_code & 0x3u) == 0x3u) {
        uint32_t cr2;

        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        if (paging_resolve_copy_on_write_in(paging_current_directory_phys(), cr2) == 0) {
            return (uint32_t)(uintptr_t)frame;
        }
    }

    if (frame->int_no == 14u && !interrupt_from_user(frame)) {
        uint32_t cr2;

        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        if (kstack_is_guard_address(cr2)) {
            console_write("kstack: guard hit at 0x");
            console_write_hex32(cr2);
            console_write("\n");
        }
    }

    print_fault_details(frame);

    if (interrupt_from_user(frame)) {
        return proc_sys_exit(frame, (int32_t)(0x80u | (frame->int_no & 0x7Fu)));
    }

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void interrupts_enable(void) {
    __asm__ volatile ("sti");
}

void interrupts_disable(void) {
    __asm__ volatile ("cli");
}
