#include "kernel/usercopy.h"

#include <stdint.h>

#include "kernel/palloc.h"
#include "kernel/paging.h"

static uint32_t min_u32(uint32_t left, uint32_t right) {
    return left < right ? left : right;
}

int user_copy_from_in(uint32_t directory_phys, void *destination, uint32_t source_address, uint32_t length) {
    uint8_t *dst = (uint8_t *)destination;
    uint32_t copied = 0u;

    if (destination == 0) {
        return -1;
    }

    while (copied < length) {
        uintptr_t virtual_address = (uintptr_t)(source_address + copied);
        uint32_t physical_page;
        uint32_t page_flags;
        uint32_t page_offset;
        uint32_t chunk;
        uint8_t *src;

        if (paging_lookup_page_in(directory_phys, virtual_address, &physical_page, &page_flags) != 0 || (page_flags & PAGE_USER) == 0u) {
            return -1;
        }

        page_offset = (uint32_t)(virtual_address & (PAGE_SIZE - 1u));
        chunk = min_u32(length - copied, PAGE_SIZE - page_offset);
        src = (uint8_t *)paging_phys_to_virt(physical_page + page_offset);

        {
            uint32_t index;

            for (index = 0u; index < chunk; index++) {
                *dst++ = *src++;
            }
        }

        copied += chunk;
    }

    return 0;
}

int user_copy_to_in(uint32_t directory_phys, uint32_t destination_address, const void *source, uint32_t length) {
    const uint8_t *src = (const uint8_t *)source;
    uint32_t copied = 0u;

    if (source == 0) {
        return -1;
    }

    while (copied < length) {
        uintptr_t virtual_address = (uintptr_t)(destination_address + copied);
        uint32_t physical_page;
        uint32_t page_flags;
        uint32_t page_offset;
        uint32_t chunk;
        uint8_t *dst;

        if (paging_lookup_page_in(directory_phys, virtual_address, &physical_page, &page_flags) != 0 ||
            (page_flags & PAGE_USER) == 0u) {
            return -1;
        }

        if ((page_flags & PAGE_WRITABLE) == 0u) {
            if ((page_flags & PAGE_COPY_ON_WRITE) == 0u ||
                paging_resolve_copy_on_write_in(directory_phys, virtual_address) != 0 ||
                paging_lookup_page_in(directory_phys, virtual_address, &physical_page, &page_flags) != 0 ||
                (page_flags & (PAGE_USER | PAGE_WRITABLE)) != (PAGE_USER | PAGE_WRITABLE)) {
                return -1;
            }
        }

        page_offset = (uint32_t)(virtual_address & (PAGE_SIZE - 1u));
        chunk = min_u32(length - copied, PAGE_SIZE - page_offset);
        dst = (uint8_t *)paging_phys_to_virt(physical_page + page_offset);

        {
            uint32_t index;

            for (index = 0u; index < chunk; index++) {
                *dst++ = *src++;
            }
        }

        copied += chunk;
    }

    return 0;
}

int user_copy_string_from_in(uint32_t directory_phys, char *destination, uint32_t source_address, uint32_t max_length) {
    uint32_t index;

    if (destination == 0 || max_length == 0u) {
        return -1;
    }

    for (index = 0u; index < max_length; index++) {
        if (user_copy_from_in(directory_phys, &destination[index], source_address + index, 1u) != 0) {
            return -1;
        }

        if (destination[index] == '\0') {
            return 0;
        }
    }

    destination[max_length - 1u] = '\0';
    return -1;
}

int user_copy_from(void *destination, uint32_t source_address, uint32_t length) {
    return user_copy_from_in(paging_current_directory_phys(), destination, source_address, length);
}

int user_copy_to(uint32_t destination_address, const void *source, uint32_t length) {
    return user_copy_to_in(paging_current_directory_phys(), destination_address, source, length);
}

int user_copy_string_from(char *destination, uint32_t source_address, uint32_t max_length) {
    return user_copy_string_from_in(paging_current_directory_phys(), destination, source_address, max_length);
}
