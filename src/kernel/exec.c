#include "kernel/exec.h"

#include <stdint.h>

#include "kernel/console.h"
#include "kernel/paging.h"
#include "kernel/palloc.h"
#include "kernel/vfs.h"

#define ELF_IDENT_SIZE 16u
#define ELF_CLASS_32 1u
#define ELF_DATA_LSB 1u
#define ELF_MACHINE_386 3u
#define ELF_TYPE_EXEC 2u
#define ELF_PROGRAM_LOAD 1u
#define ELF_FLAG_WRITE 0x2u
#define ELF_MAX_PROGRAM_HEADERS 8u

typedef struct elf32_header {
    uint8_t ident[ELF_IDENT_SIZE];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed)) elf32_header_t;

typedef struct elf32_program_header {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
} __attribute__((packed)) elf32_program_header_t;

static uintptr_t align_down(uintptr_t value, uint32_t alignment) {
    return value & ~(uintptr_t)(alignment - 1u);
}

static uintptr_t align_up(uintptr_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(uintptr_t)(alignment - 1u);
}

static void exec_reset_image(exec_image_t *image) {
    uint32_t index;

    image->entry_point = 0u;
    image->stack_top = 0u;
    image->mapped_page_count = 0u;

    for (index = 0u; index < EXEC_MAX_MAPPED_PAGES; index++) {
        image->mapped_virtuals[index] = 0u;
        image->mapped_pages[index] = 0u;
    }
}

static void zero_user_page(uint32_t physical_address) {
    uint8_t *page = (uint8_t *)paging_phys_to_virt(physical_address);
    uint32_t index;

    for (index = 0u; index < PAGE_SIZE; index++) {
        page[index] = 0u;
    }
}

static int exec_track_mapping(exec_image_t *image, uintptr_t virtual_address, uint32_t physical_address) {
    if (image->mapped_page_count >= EXEC_MAX_MAPPED_PAGES) {
        return -1;
    }

    image->mapped_virtuals[image->mapped_page_count] = virtual_address;
    image->mapped_pages[image->mapped_page_count] = physical_address;
    image->mapped_page_count++;
    return 0;
}

static uint32_t exec_mapping_flags(uint32_t page_flags) {
    uint32_t flags = PAGE_USER;

    if ((page_flags & PAGE_WRITABLE) != 0u) {
        flags |= PAGE_WRITABLE;
    }

    if ((page_flags & PAGE_COPY_ON_WRITE) != 0u) {
        flags |= PAGE_COPY_ON_WRITE;
    }

    return flags;
}

static int exec_validate_header(const elf32_header_t *header, uint32_t image_length) {
    uint32_t ph_end;

    if (image_length < sizeof(*header)) {
        return -1;
    }

    if (header->ident[0] != 0x7Fu || header->ident[1] != 'E' || header->ident[2] != 'L' || header->ident[3] != 'F') {
        return -1;
    }

    if (header->ident[4] != ELF_CLASS_32 || header->ident[5] != ELF_DATA_LSB || header->ident[6] != 1u) {
        return -1;
    }

    if (header->type != ELF_TYPE_EXEC || header->machine != ELF_MACHINE_386) {
        return -1;
    }

    if (header->phentsize != sizeof(elf32_program_header_t) || header->phnum == 0u) {
        return -1;
    }

    ph_end = header->phoff + ((uint32_t)header->phnum * header->phentsize);
    if (ph_end < header->phoff || ph_end > image_length) {
        return -1;
    }

    return 0;
}

static int exec_segment_in_user_range(const elf32_program_header_t *program_header) {
    uintptr_t segment_start;
    uintptr_t segment_end;
    uintptr_t stack_guard = EXEC_USER_STACK_TOP - PAGE_SIZE;

    if (program_header->memsz < program_header->filesz) {
        return 0;
    }

    segment_start = (uintptr_t)program_header->vaddr;
    segment_end = segment_start + (uintptr_t)program_header->memsz;

    if (segment_end < segment_start) {
        return 0;
    }

    if (segment_start < USER_VIRT_BASE || segment_end > stack_guard) {
        return 0;
    }

    return 1;
}

static int exec_read_exact(vfs_file_t *file, uint32_t offset, void *buffer, uint32_t length) {
    int32_t read_result;

    file->offset = offset;
    read_result = vfs_read_file(file, buffer, length);
    return read_result == (int32_t)length ? 0 : -1;
}

static int exec_map_segment(uint32_t directory_phys, vfs_file_t *file, uint32_t image_length, const elf32_program_header_t *program_header, exec_image_t *image) {
    uintptr_t segment_page;
    uintptr_t segment_end;
    uint32_t file_offset;
    uint32_t remaining_file_bytes;
    uintptr_t virtual_address;

    if (!exec_segment_in_user_range(program_header)) {
        return -1;
    }

    if ((program_header->offset + program_header->filesz) > image_length) {
        return -1;
    }

    segment_page = align_down((uintptr_t)program_header->vaddr, PAGE_SIZE);
    segment_end = align_up((uintptr_t)program_header->vaddr + (uintptr_t)program_header->memsz, PAGE_SIZE);

    while (segment_page < segment_end) {
        uint32_t physical_page;
        uint32_t page_flags = PAGE_USER;

        if (paging_resolve_physical_in(directory_phys, segment_page) != 0u) {
            return -1;
        }

        physical_page = palloc_alloc_page();
        if (physical_page == 0u) {
            return -1;
        }

        zero_user_page(physical_page);

        if ((program_header->flags & ELF_FLAG_WRITE) != 0u) {
            page_flags |= PAGE_WRITABLE;
        }

        if (paging_map_page_in(directory_phys, segment_page, physical_page, page_flags) != 0) {
            palloc_free_page(physical_page);
            return -1;
        }

        if (exec_track_mapping(image, segment_page, physical_page) != 0) {
            paging_unmap_page_in(directory_phys, segment_page);
            palloc_free_page(physical_page);
            return -1;
        }

        segment_page += PAGE_SIZE;
    }

    file_offset = program_header->offset;
    remaining_file_bytes = program_header->filesz;
    virtual_address = (uintptr_t)program_header->vaddr;

    while (remaining_file_bytes != 0u) {
        uint32_t physical_page = paging_resolve_physical_in(directory_phys, virtual_address);
        uint8_t *page_bytes;
        uint32_t page_offset;
        uint32_t chunk;

        if (physical_page == 0u) {
            return -1;
        }

        page_bytes = (uint8_t *)paging_phys_to_virt(physical_page);
        page_offset = virtual_address & (PAGE_SIZE - 1u);
        chunk = PAGE_SIZE - page_offset;
        if (chunk > remaining_file_bytes) {
            chunk = remaining_file_bytes;
        }

        if (exec_read_exact(file, file_offset, &page_bytes[page_offset], chunk) != 0) {
            return -1;
        }

        file_offset += chunk;
        virtual_address += chunk;
        remaining_file_bytes -= chunk;
    }

    return 0;
}

static int exec_map_stack(uint32_t directory_phys, exec_image_t *image) {
    uintptr_t stack_page = EXEC_USER_STACK_TOP - PAGE_SIZE;
    uint32_t physical_page;

    physical_page = palloc_alloc_page();
    if (physical_page == 0u) {
        return -1;
    }

    zero_user_page(physical_page);

    if (paging_map_page_in(directory_phys, stack_page, physical_page, PAGE_USER | PAGE_WRITABLE) != 0) {
        palloc_free_page(physical_page);
        return -1;
    }

    if (exec_track_mapping(image, stack_page, physical_page) != 0) {
        paging_unmap_page_in(directory_phys, stack_page);
        palloc_free_page(physical_page);
        return -1;
    }

    image->stack_top = EXEC_USER_STACK_TOP;
    return 0;
}

void exec_release_image(uint32_t directory_phys, exec_image_t *image) {
    uint32_t index;

    if (image == 0) {
        return;
    }

    for (index = 0u; index < image->mapped_page_count; index++) {
        if (image->mapped_pages[index] != 0u) {
            paging_unmap_page_in(directory_phys, image->mapped_virtuals[index]);
            palloc_free_page(image->mapped_pages[index]);
        }
    }

    exec_reset_image(image);
}

int exec_load_path(uint32_t directory_phys, const char *path, exec_image_t *image) {
    elf32_header_t header;
    elf32_program_header_t program_headers[ELF_MAX_PROGRAM_HEADERS];
    vfs_file_t *file;
    uint32_t image_length;
    uint32_t index;
    uint32_t loadable_segments = 0u;

    if (directory_phys == 0u || path == 0 || image == 0) {
        return -1;
    }

    exec_reset_image(image);

    if (vfs_open_path(&file, path, VFS_O_RDONLY) != 0) {
        console_write("exec: missing ");
        console_write(path);
        console_write("\n");
        return -1;
    }

    image_length = file->inode->size;
    if (exec_read_exact(file, 0u, &header, sizeof(header)) != 0) {
        vfs_close_file(file);
        console_write("exec: header read failed ");
        console_write(path);
        console_write("\n");
        return -1;
    }

    if (exec_validate_header(&header, image_length) != 0 || header.phnum > ELF_MAX_PROGRAM_HEADERS) {
        vfs_close_file(file);
        console_write("exec: header invalid ");
        console_write(path);
        console_write("\n");
        return -1;
    }

    if (exec_read_exact(file, header.phoff, program_headers, header.phnum * sizeof(elf32_program_header_t)) != 0) {
        vfs_close_file(file);
        console_write("exec: phdr read failed ");
        console_write(path);
        console_write("\n");
        return -1;
    }

    for (index = 0u; index < header.phnum; index++) {
        if (program_headers[index].type != ELF_PROGRAM_LOAD) {
            continue;
        }

        if (exec_map_segment(directory_phys, file, image_length, &program_headers[index], image) != 0) {
            vfs_close_file(file);
            exec_release_image(directory_phys, image);
            console_write("exec: load failed ");
            console_write(path);
            console_write("\n");
            return -1;
        }

        loadable_segments++;
    }

    if (loadable_segments == 0u || exec_map_stack(directory_phys, image) != 0) {
        vfs_close_file(file);
        exec_release_image(directory_phys, image);
        console_write("exec: stack failed ");
        console_write(path);
        console_write("\n");
        return -1;
    }

    image->entry_point = (uintptr_t)header.entry;
    vfs_close_file(file);

    console_write("exec: loaded ");
    console_write(path);
    console_write(" entry 0x");
    console_write_hex32((uint32_t)image->entry_point);
    console_write("\n");

    return 0;
}

int exec_clone_image(uint32_t source_directory_phys, uint32_t target_directory_phys, const exec_image_t *source_image, exec_image_t *target_image) {
    uint32_t index;

    if (source_directory_phys == 0u || target_directory_phys == 0u || source_image == 0 || target_image == 0) {
        return -1;
    }

    exec_reset_image(target_image);
    target_image->entry_point = source_image->entry_point;
    target_image->stack_top = source_image->stack_top;

    for (index = 0u; index < source_image->mapped_page_count; index++) {
        uintptr_t virtual_address = source_image->mapped_virtuals[index];
        uint32_t source_page;
        uint32_t page_flags;
        uint32_t target_flags;

        if (paging_lookup_page_in(source_directory_phys, virtual_address, &source_page, &page_flags) != 0) {
            exec_release_image(target_directory_phys, target_image);
            return -1;
        }

        target_flags = exec_mapping_flags(page_flags);
        if ((target_flags & PAGE_WRITABLE) != 0u) {
            target_flags &= ~PAGE_WRITABLE;
            target_flags |= PAGE_COPY_ON_WRITE;

            if (paging_map_page_in(source_directory_phys, virtual_address, source_page, target_flags) != 0) {
                exec_release_image(target_directory_phys, target_image);
                return -1;
            }
        }

        if (palloc_retain_page(source_page) != 0) {
            exec_release_image(target_directory_phys, target_image);
            return -1;
        }

        if (paging_map_page_in(target_directory_phys, virtual_address, source_page, target_flags) != 0) {
            palloc_free_page(source_page);
            exec_release_image(target_directory_phys, target_image);
            return -1;
        }

        if (exec_track_mapping(target_image, virtual_address, source_page) != 0) {
            paging_unmap_page_in(target_directory_phys, virtual_address);
            palloc_free_page(source_page);
            exec_release_image(target_directory_phys, target_image);
            return -1;
        }
    }

    return 0;
}
