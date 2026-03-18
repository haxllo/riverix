#ifndef RIVERIX_PAGING_H
#define RIVERIX_PAGING_H

#include <stdint.h>

#define PAGE_PRESENT 0x001u
#define PAGE_WRITABLE 0x002u
#define PAGE_USER 0x004u
#define PAGE_CACHE_DISABLE 0x010u
#define PAGE_COPY_ON_WRITE 0x200u
#define KERNEL_DIRECT_MAP_BASE 0xC0000000u
#define KERNEL_MMIO_BASE 0xF7000000u
#define KERNEL_MMIO_LIMIT 0xF7800000u
#define KERNEL_HEAP_BASE 0xF8000000u
#define KERNEL_HEAP_LIMIT 0xFC000000u
#define KERNEL_STACK_BASE 0xFC000000u
#define KERNEL_RECURSIVE_PD_BASE 0xFFFFF000u
#define KERNEL_RECURSIVE_PT_BASE 0xFFC00000u
#define KERNEL_STACK_LIMIT KERNEL_RECURSIVE_PT_BASE
#define USER_VIRT_BASE 0x00400000u
#define USER_VIRT_LIMIT KERNEL_DIRECT_MAP_BASE

void paging_init(void);
uint32_t paging_page_directory_phys(void);
uint32_t paging_current_directory_phys(void);
uint32_t paging_direct_map_limit(void);
uintptr_t paging_phys_to_virt(uint32_t physical_address);
uint32_t paging_virt_to_phys(uintptr_t virtual_address);
uint32_t paging_create_address_space(void);
void paging_destroy_address_space(uint32_t directory_phys);
void paging_switch_directory(uint32_t directory_phys);
int paging_lookup_page_in(uint32_t directory_phys, uintptr_t virtual_address, uint32_t *out_physical_address, uint32_t *out_flags);
uint32_t paging_resolve_physical_in(uint32_t directory_phys, uintptr_t virtual_address);
int paging_map_page_in(uint32_t directory_phys, uintptr_t virtual_address, uint32_t physical_address, uint32_t flags);
int paging_map_page(uintptr_t virtual_address, uint32_t physical_address, uint32_t flags);
int paging_map_pages_in(uint32_t directory_phys, uintptr_t virtual_address, const uint32_t *physical_pages, uint32_t page_count, uint32_t flags);
int paging_map_pages(uintptr_t virtual_address, const uint32_t *physical_pages, uint32_t page_count, uint32_t flags);
void paging_unmap_page_in(uint32_t directory_phys, uintptr_t virtual_address);
void paging_unmap_page(uintptr_t virtual_address);
void paging_unmap_pages_in(uint32_t directory_phys, uintptr_t virtual_address, uint32_t page_count);
void paging_unmap_pages(uintptr_t virtual_address, uint32_t page_count);
int paging_range_present_in(uint32_t directory_phys, uintptr_t virtual_address, uint32_t page_count, uint32_t required_flags);
int paging_user_range_mapped_in(uint32_t directory_phys, uintptr_t virtual_address, uint32_t length);
int paging_user_range_mapped(uintptr_t virtual_address, uint32_t length);
int paging_user_range_writable_in(uint32_t directory_phys, uintptr_t virtual_address, uint32_t length);
int paging_user_range_writable(uintptr_t virtual_address, uint32_t length);
int paging_resolve_copy_on_write_in(uint32_t directory_phys, uintptr_t virtual_address);

#endif
