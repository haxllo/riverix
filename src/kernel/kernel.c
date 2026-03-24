#include <stdint.h>

#include "kernel/bootinfo.h"
#include "kernel/console.h"
#include "kernel/gdt.h"
#include "kernel/idt.h"
#include "kernel/kheap.h"
#include "kernel/kstack.h"
#include "kernel/memory.h"
#include "kernel/mmio.h"
#include "kernel/multiboot.h"
#include "kernel/net.h"
#include "kernel/panic.h"
#include "kernel/palloc.h"
#include "kernel/paging.h"
#include "kernel/pic.h"
#include "kernel/pit.h"
#include "kernel/proc.h"
#include "kernel/trace.h"
#include "kernel/vfs.h"

extern uint8_t __kernel_start;
extern uint8_t __kernel_end;

static void idle(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr) {
    const multiboot_info_t *multiboot_info = (const multiboot_info_t *)(uintptr_t)multiboot_info_addr;
    uint32_t probe_page = 0u;
    const multiboot_info_t *high_multiboot_info;

    console_init();
    trace_init();

    console_write("riverix: kernel_main reached\n");
    console_write("riverix: multiboot magic 0x");
    console_write_hex32(multiboot_magic);
    console_write("\n");

    console_write("riverix: multiboot info  0x");
    console_write_hex32(multiboot_info_addr);
    console_write("\n");

    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        console_write("riverix: invalid multiboot magic, refusing to continue\n");
        panic("invalid multiboot magic");
    }

    memory_report(multiboot_info);
    palloc_init(multiboot_info, (uintptr_t)&__kernel_start, (uintptr_t)&__kernel_end);

    console_write("palloc: usable pages 0x");
    console_write_hex32(palloc_total_usable_pages());
    console_write("\n");

    console_write("palloc: free pages   0x");
    console_write_hex32(palloc_free_pages());
    console_write("\n");
    trace_log(SYS_TRACE_CATEGORY_MEMORY,
              SYS_TRACE_EVENT_MEMORY_STATE,
              palloc_total_usable_pages(),
              palloc_free_pages(),
              probe_page);

    probe_page = palloc_alloc_page();
    console_write("palloc: probe alloc  0x");
    console_write_hex32(probe_page);
    console_write("\n");

    if (probe_page != 0u) {
        palloc_free_page(probe_page);
    }

    console_write("palloc: free pages   0x");
    console_write_hex32(palloc_free_pages());
    console_write("\n");

    paging_init();
    high_multiboot_info = (const multiboot_info_t *)paging_phys_to_virt(multiboot_info_addr);

    console_write("paging: high alias flags 0x");
    console_write_hex32(high_multiboot_info->flags);
    console_write("\n");

    kheap_init();
    (void)kheap_selftest();
    kstack_init();
    (void)kstack_selftest();
    mmio_init();

    bootinfo_init(high_multiboot_info);
    gdt_init();
    idt_init();
    pic_init();
    pit_init(100u);
    (void)net_init();
    vfs_init(high_multiboot_info);
    (void)vfs_storage_selftest();
    proc_init();
    proc_start_boot_tasks();
    interrupts_enable();

    console_write("riverix: next steps -> syscall/vfs growth, userland, install path\n");

    idle();
}
