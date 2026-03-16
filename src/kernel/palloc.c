#include "kernel/palloc.h"

#include <stdint.h>

#include "kernel/console.h"

#define MAX_PHYSICAL_BYTES 0x100000000ull
#define MAX_PAGE_FRAMES (MAX_PHYSICAL_BYTES / PAGE_SIZE)
#define BITMAP_WORD_BITS 32u
#define BITMAP_WORDS (MAX_PAGE_FRAMES / BITMAP_WORD_BITS)

static uint32_t frame_bitmap[BITMAP_WORDS];
static uint16_t frame_refcounts[MAX_PAGE_FRAMES];
static uint32_t first_unusable_frame;
static uint32_t total_usable_frames;
static uint32_t free_frames;
static uint32_t next_search_frame;

static uint32_t frame_word_index(uint32_t frame) {
    return frame / BITMAP_WORD_BITS;
}

static uint32_t frame_bit_mask(uint32_t frame) {
    return 1u << (frame % BITMAP_WORD_BITS);
}

static int frame_is_used(uint32_t frame) {
    return (frame_bitmap[frame_word_index(frame)] & frame_bit_mask(frame)) != 0u;
}

static void mark_frame_used(uint32_t frame) {
    frame_bitmap[frame_word_index(frame)] |= frame_bit_mask(frame);
}

static void mark_frame_free(uint32_t frame) {
    frame_bitmap[frame_word_index(frame)] &= ~frame_bit_mask(frame);
}

static uint64_t clip_range_end(uint64_t end) {
    if (end > MAX_PHYSICAL_BYTES) {
        return MAX_PHYSICAL_BYTES;
    }

    return end;
}

static uint32_t align_up_to_page(uint64_t address) {
    return (uint32_t)((address + PAGE_SIZE - 1u) / PAGE_SIZE);
}

static uint32_t align_down_to_page(uint64_t address) {
    return (uint32_t)(address / PAGE_SIZE);
}

static void free_range(uint64_t start, uint64_t end) {
    uint32_t frame;
    uint32_t first_frame = align_up_to_page(start);
    uint32_t last_frame = align_down_to_page(clip_range_end(end));

    if (last_frame > first_unusable_frame) {
        first_unusable_frame = last_frame;
    }

    for (frame = first_frame; frame < last_frame; frame++) {
        if (frame_is_used(frame)) {
            mark_frame_free(frame);
            frame_refcounts[frame] = 0u;
            free_frames++;
        }
    }
}

static void reserve_range(uint64_t start, uint64_t end) {
    uint32_t frame;
    uint32_t first_frame = align_down_to_page(start);
    uint32_t last_frame = align_up_to_page(clip_range_end(end));

    if (last_frame > first_unusable_frame) {
        last_frame = first_unusable_frame;
    }

    for (frame = first_frame; frame < last_frame; frame++) {
        if (!frame_is_used(frame)) {
            mark_frame_used(frame);
            frame_refcounts[frame] = 1u;
            free_frames--;
        }
    }
}

static void reserve_boot_structures(const multiboot_info_t *multiboot_info) {
    const multiboot_module_t *modules;
    uint32_t index;

    reserve_range((uintptr_t)multiboot_info, (uintptr_t)multiboot_info + sizeof(*multiboot_info));

    if ((multiboot_info->flags & MULTIBOOT_INFO_MMAP) != 0u) {
        reserve_range(multiboot_info->mmap_addr, multiboot_info->mmap_addr + multiboot_info->mmap_length);
    }

    if ((multiboot_info->flags & MULTIBOOT_INFO_CMDLINE) != 0u && multiboot_info->cmdline != 0u) {
        reserve_range(multiboot_info->cmdline, multiboot_info->cmdline + 256u);
    }

    if ((multiboot_info->flags & MULTIBOOT_INFO_MODS) == 0u || multiboot_info->mods_count == 0u) {
        return;
    }

    reserve_range(multiboot_info->mods_addr, multiboot_info->mods_addr + (multiboot_info->mods_count * sizeof(multiboot_module_t)));

    modules = multiboot_module_begin(multiboot_info);
    for (index = 0u; index < multiboot_info->mods_count; index++) {
        reserve_range(modules[index].mod_start, modules[index].mod_end);

        if (modules[index].cmdline != 0u) {
            reserve_range(modules[index].cmdline, modules[index].cmdline + 256u);
        }
    }
}

void palloc_init(const multiboot_info_t *multiboot_info, uintptr_t kernel_start, uintptr_t kernel_end) {
    uint32_t word;
    uint32_t frame;

    for (word = 0; word < BITMAP_WORDS; word++) {
        frame_bitmap[word] = 0xFFFFFFFFu;
    }

    for (frame = 0u; frame < MAX_PAGE_FRAMES; frame++) {
        frame_refcounts[frame] = 1u;
    }

    first_unusable_frame = 0u;
    total_usable_frames = 0u;
    free_frames = 0u;
    next_search_frame = 0u;

    if ((multiboot_info->flags & MULTIBOOT_INFO_MMAP) != 0u) {
        const multiboot_memory_map_t *entry = multiboot_mmap_begin(multiboot_info);

        while (entry < multiboot_mmap_end(multiboot_info)) {
            if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                free_range(entry->addr, entry->addr + entry->len);
            }

            entry = multiboot_mmap_next(entry);
        }
    } else if ((multiboot_info->flags & MULTIBOOT_INFO_MEMORY) != 0u) {
        uint64_t upper_end = 0x100000ull + ((uint64_t)multiboot_info->mem_upper * 1024ull);
        free_range(0x100000ull, upper_end);
    }

    total_usable_frames = free_frames;

    reserve_range(0u, 0x100000ull);
    reserve_range(kernel_start, kernel_end);
    reserve_boot_structures(multiboot_info);

    next_search_frame = align_up_to_page(kernel_end);

    console_write("palloc: kernel reserved 0x");
    console_write_hex32((uint32_t)kernel_start);
    console_write("-0x");
    console_write_hex32((uint32_t)kernel_end);
    console_write("\n");
}

uint32_t palloc_alloc_page(void) {
    uint32_t frame;

    for (frame = next_search_frame; frame < first_unusable_frame; frame++) {
        if (!frame_is_used(frame)) {
            mark_frame_used(frame);
            frame_refcounts[frame] = 1u;
            free_frames--;
            next_search_frame = frame + 1u;
            return frame * PAGE_SIZE;
        }
    }

    for (frame = 0u; frame < next_search_frame; frame++) {
        if (!frame_is_used(frame)) {
            mark_frame_used(frame);
            frame_refcounts[frame] = 1u;
            free_frames--;
            next_search_frame = frame + 1u;
            return frame * PAGE_SIZE;
        }
    }

    return 0u;
}

int palloc_retain_page(uint32_t physical_address) {
    uint32_t frame;

    if ((physical_address % PAGE_SIZE) != 0u) {
        return -1;
    }

    frame = physical_address / PAGE_SIZE;
    if (frame >= first_unusable_frame) {
        return -1;
    }

    if (!frame_is_used(frame) || frame_refcounts[frame] == 0u) {
        return -1;
    }

    frame_refcounts[frame]++;
    return 0;
}

void palloc_free_page(uint32_t physical_address) {
    uint32_t frame;

    if ((physical_address % PAGE_SIZE) != 0u) {
        return;
    }

    frame = physical_address / PAGE_SIZE;
    if (frame >= first_unusable_frame) {
        return;
    }

    if (!frame_is_used(frame) || frame_refcounts[frame] == 0u) {
        return;
    }

    frame_refcounts[frame]--;
    if (frame_refcounts[frame] != 0u) {
        return;
    }

    mark_frame_free(frame);
    free_frames++;

    if (frame < next_search_frame) {
        next_search_frame = frame;
    }
}

uint32_t palloc_page_refcount(uint32_t physical_address) {
    uint32_t frame;

    if ((physical_address % PAGE_SIZE) != 0u) {
        return 0u;
    }

    frame = physical_address / PAGE_SIZE;
    if (frame >= first_unusable_frame || !frame_is_used(frame)) {
        return 0u;
    }

    return frame_refcounts[frame];
}

uint32_t palloc_total_usable_pages(void) {
    return total_usable_frames;
}

uint32_t palloc_free_pages(void) {
    return free_frames;
}

uint32_t palloc_managed_bytes(void) {
    return first_unusable_frame * PAGE_SIZE;
}
