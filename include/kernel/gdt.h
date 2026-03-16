#ifndef RIVERIX_GDT_H
#define RIVERIX_GDT_H

#include <stdint.h>

#define GDT_KERNEL_CODE_SELECTOR 0x08u
#define GDT_KERNEL_DATA_SELECTOR 0x10u
#define GDT_USER_CODE_SELECTOR 0x1Bu
#define GDT_USER_DATA_SELECTOR 0x23u
#define GDT_TSS_SELECTOR 0x28u

void gdt_init(void);
void gdt_set_kernel_stack(uint32_t stack_top);

#endif
