#ifndef RIVERIX_PALLOC_H
#define RIVERIX_PALLOC_H

#include <stdint.h>

#include "kernel/multiboot.h"

#define PAGE_SIZE 4096u

void palloc_init(const multiboot_info_t *multiboot_info, uintptr_t kernel_start, uintptr_t kernel_end);
uint32_t palloc_alloc_page(void);
int palloc_retain_page(uint32_t physical_address);
void palloc_free_page(uint32_t physical_address);
uint32_t palloc_page_refcount(uint32_t physical_address);
uint32_t palloc_total_usable_pages(void);
uint32_t palloc_free_pages(void);
uint32_t palloc_managed_bytes(void);

#endif
