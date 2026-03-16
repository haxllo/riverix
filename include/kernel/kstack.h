#ifndef RIVERIX_KSTACK_H
#define RIVERIX_KSTACK_H

#include <stdint.h>

typedef struct kernel_stack {
    uintptr_t guard_base;
    uintptr_t stack_base;
    uintptr_t stack_top;
} kernel_stack_t;

void kstack_init(void);
int kstack_selftest(void);
int kstack_alloc(kernel_stack_t *stack);
void kstack_free(kernel_stack_t *stack);
int kstack_is_guard_address(uintptr_t virtual_address);

#endif
