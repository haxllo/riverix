#ifndef RIVERIX_IDT_H
#define RIVERIX_IDT_H

#include <stdint.h>

typedef struct interrupt_frame {
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t int_no;
    uint32_t err_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
} interrupt_frame_t;

typedef struct interrupt_user_frame {
    interrupt_frame_t base;
    uint32_t user_esp;
    uint32_t user_ss;
} interrupt_user_frame_t;

static inline int interrupt_from_user(const interrupt_frame_t *frame) {
    return (frame->cs & 3u) == 3u;
}

void idt_init(void);
uint32_t interrupt_dispatch(interrupt_frame_t *frame);
void interrupts_enable(void);
void interrupts_disable(void);

#endif
