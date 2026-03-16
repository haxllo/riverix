#ifndef RIVERIX_KHEAP_H
#define RIVERIX_KHEAP_H

#include <stddef.h>
#include <stdint.h>

void kheap_init(void);
int kheap_selftest(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void kfree(void *pointer);
uint32_t kheap_committed_pages(void);

#endif
