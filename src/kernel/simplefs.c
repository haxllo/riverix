#include "kernel/simplefs.h"

#include <stdint.h>

#include "kernel/console.h"
#include "shared/simplefs_format.h"

#define SIMPLEFS_MAX_MOUNT_INODES 64u
#define SIMPLEFS_DIRECTORY_CAPACITY 16u
#define SIMPLEFS_MAX_DIRECTORY_SLOTS 16u
#define SIMPLEFS_MAX_MOUNT_DIRENTS (SIMPLEFS_DIRECTORY_CAPACITY * SIMPLEFS_MAX_DIRECTORY_SLOTS)
#define SIMPLEFS_MAX_MOUNT_BLOCKS 32768u
#define SIMPLEFS_MAX_BLOCK_BITMAP_BYTES (SIMPLEFS_MAX_MOUNT_BLOCKS / 8u)
#define SIMPLEFS_INVALID_DIR_SLOT 0xFFFFFFFFu

typedef struct simplefs_runtime_inode {
    simplefs_inode_disk_t disk;
    vfs_inode_t vfs;
    char name[SIMPLEFS_NAME_MAX + 1u];
    uint32_t inode_index;
    uint32_t populated;
    uint32_t dir_slot_base;
} simplefs_runtime_inode_t;

static simplefs_runtime_inode_t runtime_inodes[SIMPLEFS_MAX_MOUNT_INODES];
static vfs_dir_entry_t runtime_dir_entries[SIMPLEFS_MAX_MOUNT_DIRENTS];
static simplefs_inode_disk_t inode_table_image[SIMPLEFS_MAX_MOUNT_INODES];
static block_device_t *mounted_device;
static simplefs_superblock_t mounted_superblock;
static uint8_t inode_bitmap[SIMPLEFS_BLOCK_SIZE];
static uint8_t block_bitmap[SIMPLEFS_MAX_BLOCK_BITMAP_BYTES];
static uint32_t mounted_inode_count;
static uint32_t next_dir_entry_slot;
static uint8_t block_scratch[SIMPLEFS_BLOCK_SIZE];

static int32_t simplefs_regular_read(vfs_file_t *file, void *buffer, uint32_t length);
static int32_t simplefs_regular_write(vfs_file_t *file, const char *buffer, uint32_t length);
static int32_t simplefs_create_child(vfs_inode_t *directory, const char *name, vfs_inode_kind_t kind, vfs_inode_t **out_inode);
static int32_t simplefs_remove_child(vfs_inode_t *directory, const char *name);
static int32_t simplefs_resize_inode(vfs_inode_t *inode, uint32_t size);

static const vfs_file_ops_t simplefs_file_ops = {
    .read = simplefs_regular_read,
    .write = simplefs_regular_write,
};

static const vfs_inode_ops_t simplefs_inode_ops = {
    .create_child = simplefs_create_child,
    .remove_child = simplefs_remove_child,
    .resize = simplefs_resize_inode,
};

static uint32_t string_length(const char *text) {
    uint32_t length = 0u;

    while (text[length] != '\0') {
        length++;
    }

    return length;
}

static int string_equals(const char *left, const char *right) {
    uint32_t index = 0u;

    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }

        index++;
    }

    return left[index] == right[index];
}

static void zero_bytes(void *buffer, uint32_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    uint32_t index;

    for (index = 0u; index < length; index++) {
        bytes[index] = 0u;
    }
}

static void copy_bytes(void *destination, const void *source, uint32_t length) {
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    uint32_t index;

    for (index = 0u; index < length; index++) {
        dst[index] = src[index];
    }
}

static uint32_t min_u32(uint32_t left, uint32_t right) {
    return left < right ? left : right;
}

static uint32_t align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uint32_t simplefs_required_blocks(uint32_t size) {
    if (size == 0u) {
        return 0u;
    }

    return align_up(size, SIMPLEFS_BLOCK_SIZE) / SIMPLEFS_BLOCK_SIZE;
}

static int simplefs_inode_used(uint32_t inode_index) {
    return (inode_bitmap[inode_index / 8u] & (uint8_t)(1u << (inode_index % 8u))) != 0u;
}

static void simplefs_set_inode_used(uint32_t inode_index, int used) {
    if (used != 0) {
        inode_bitmap[inode_index / 8u] |= (uint8_t)(1u << (inode_index % 8u));
    } else {
        inode_bitmap[inode_index / 8u] &= (uint8_t)~(1u << (inode_index % 8u));
    }
}

static int simplefs_block_used(uint32_t block_index) {
    return (block_bitmap[block_index / 8u] & (uint8_t)(1u << (block_index % 8u))) != 0u;
}

static void simplefs_set_block_used(uint32_t block_index, int used) {
    if (used != 0) {
        block_bitmap[block_index / 8u] |= (uint8_t)(1u << (block_index % 8u));
    } else {
        block_bitmap[block_index / 8u] &= (uint8_t)~(1u << (block_index % 8u));
    }
}

static int32_t simplefs_read_blocks(uint32_t start_block, uint32_t block_count, void *buffer) {
    if (mounted_device == 0) {
        return -1;
    }

    return block_read(mounted_device, start_block, block_count, buffer);
}

static int32_t simplefs_write_blocks(uint32_t start_block, uint32_t block_count, const void *buffer) {
    if (mounted_device == 0 || mounted_device->read_only != 0u) {
        return -1;
    }

    return block_write(mounted_device, start_block, block_count, buffer);
}

static int32_t simplefs_flush_inode_table(void) {
    uint32_t index;

    for (index = 0u; index < mounted_inode_count; index++) {
        inode_table_image[index] = runtime_inodes[index].disk;
    }

    return simplefs_write_blocks(mounted_superblock.inode_table_start, mounted_superblock.inode_table_blocks, inode_table_image);
}

static int32_t simplefs_flush_inode_bitmap(void) {
    return simplefs_write_blocks(mounted_superblock.inode_bitmap_start, mounted_superblock.inode_bitmap_blocks, inode_bitmap);
}

static int32_t simplefs_flush_block_bitmap(void) {
    return simplefs_write_blocks(mounted_superblock.block_bitmap_start, mounted_superblock.block_bitmap_blocks, block_bitmap);
}

static int32_t simplefs_flush_superblock(void) {
    zero_bytes(block_scratch, sizeof(block_scratch));
    copy_bytes(block_scratch, &mounted_superblock, sizeof(mounted_superblock));
    return simplefs_write_blocks(0u, 1u, block_scratch);
}

static int32_t simplefs_flush_metadata(void) {
    if (mounted_device == 0 || mounted_device->read_only != 0u) {
        return -1;
    }

    if (simplefs_flush_superblock() != 0) {
        return -1;
    }

    if (simplefs_flush_inode_table() != 0) {
        return -1;
    }

    if (simplefs_flush_inode_bitmap() != 0) {
        return -1;
    }

    return simplefs_flush_block_bitmap();
}

static int32_t simplefs_zero_block(uint32_t block_index) {
    zero_bytes(block_scratch, sizeof(block_scratch));
    return simplefs_write_blocks(block_index, 1u, block_scratch);
}

static int simplefs_extent_is_free(uint32_t start_block, uint32_t block_count) {
    uint32_t index;

    if (start_block < mounted_superblock.data_block_start || (start_block + block_count) > mounted_superblock.total_blocks) {
        return 0;
    }

    for (index = 0u; index < block_count; index++) {
        if (simplefs_block_used(start_block + index)) {
            return 0;
        }
    }

    return 1;
}

static void simplefs_mark_extent(uint32_t start_block, uint32_t block_count, int used) {
    uint32_t index;

    for (index = 0u; index < block_count; index++) {
        simplefs_set_block_used(start_block + index, used);
    }
}

static int32_t simplefs_allocate_extent(uint32_t block_count, uint32_t *out_start_block) {
    uint32_t start_block;

    if (block_count == 0u || out_start_block == 0) {
        return -1;
    }

    for (start_block = mounted_superblock.data_block_start; (start_block + block_count) <= mounted_superblock.total_blocks; start_block++) {
        if (!simplefs_extent_is_free(start_block, block_count)) {
            continue;
        }

        simplefs_mark_extent(start_block, block_count, 1);
        *out_start_block = start_block;
        return 0;
    }

    return -1;
}

static void simplefs_release_extent(simplefs_runtime_inode_t *node) {
    if (node->disk.block_count != 0u) {
        simplefs_mark_extent(node->disk.data_block, node->disk.block_count, 0);
    }

    node->disk.data_block = 0u;
    node->disk.block_count = 0u;
}

static int32_t simplefs_copy_extent(uint32_t from_block, uint32_t to_block, uint32_t block_count) {
    uint32_t index;

    for (index = 0u; index < block_count; index++) {
        if (simplefs_read_blocks(from_block + index, 1u, block_scratch) != 0) {
            return -1;
        }

        if (simplefs_write_blocks(to_block + index, 1u, block_scratch) != 0) {
            return -1;
        }
    }

    return 0;
}

static void simplefs_set_inode_name(simplefs_runtime_inode_t *node, const char *name) {
    uint32_t length = string_length(name);

    zero_bytes(node->name, sizeof(node->name));
    copy_bytes(node->name, name, length);
    node->vfs.name = node->name;
}

static int32_t simplefs_alloc_dir_slots(simplefs_runtime_inode_t *directory) {
    if (directory->dir_slot_base != SIMPLEFS_INVALID_DIR_SLOT) {
        return 0;
    }

    if ((next_dir_entry_slot + SIMPLEFS_DIRECTORY_CAPACITY) > SIMPLEFS_MAX_MOUNT_DIRENTS) {
        return -1;
    }

    directory->dir_slot_base = next_dir_entry_slot;
    next_dir_entry_slot += SIMPLEFS_DIRECTORY_CAPACITY;
    directory->vfs.children = &runtime_dir_entries[directory->dir_slot_base];
    directory->vfs.child_capacity = SIMPLEFS_DIRECTORY_CAPACITY;
    return 0;
}

static int32_t simplefs_read_dir_entries(simplefs_runtime_inode_t *directory, simplefs_dir_entry_disk_t *entries, uint32_t entry_count) {
    if (entry_count == 0u) {
        return 0;
    }

    if (directory->disk.data_block == 0u || directory->disk.block_count != 1u) {
        return -1;
    }

    zero_bytes(entries, SIMPLEFS_BLOCK_SIZE);
    return simplefs_read_blocks(directory->disk.data_block, 1u, entries);
}

static int32_t simplefs_write_dir_entries(simplefs_runtime_inode_t *directory, const simplefs_dir_entry_disk_t *entries, uint32_t entry_count) {
    uint32_t required_blocks = entry_count == 0u ? 0u : 1u;

    if (mounted_device == 0 || mounted_device->read_only != 0u || directory->disk.kind != SIMPLEFS_INODE_DIR) {
        return -1;
    }

    if (entry_count > SIMPLEFS_DIRECTORY_CAPACITY) {
        return -1;
    }

    if (required_blocks == 0u) {
        if (directory->disk.block_count != 0u) {
            simplefs_release_extent(directory);
        }
    } else if (directory->disk.block_count == 0u) {
        uint32_t start_block;

        if (simplefs_allocate_extent(1u, &start_block) != 0) {
            return -1;
        }

        directory->disk.data_block = start_block;
        directory->disk.block_count = 1u;
        if (simplefs_zero_block(directory->disk.data_block) != 0) {
            simplefs_release_extent(directory);
            return -1;
        }
    }

    if (required_blocks != 0u) {
        zero_bytes(block_scratch, sizeof(block_scratch));
        copy_bytes(block_scratch, entries, entry_count * sizeof(simplefs_dir_entry_disk_t));
        if (simplefs_write_blocks(directory->disk.data_block, 1u, block_scratch) != 0) {
            return -1;
        }
    }

    directory->disk.child_count = entry_count;
    directory->disk.size = entry_count * sizeof(simplefs_dir_entry_disk_t);
    directory->vfs.size = directory->disk.size;
    return 0;
}

static int32_t simplefs_populate_directory(simplefs_runtime_inode_t *directory) {
    simplefs_dir_entry_disk_t entries[SIMPLEFS_DIRECTORY_CAPACITY];
    uint32_t index;

    if (directory->disk.kind != SIMPLEFS_INODE_DIR) {
        return -1;
    }

    if (directory->populated != 0u) {
        return 0;
    }

    if (directory->disk.child_count > SIMPLEFS_DIRECTORY_CAPACITY) {
        return -1;
    }

    if (simplefs_alloc_dir_slots(directory) != 0) {
        return -1;
    }

    zero_bytes(directory->vfs.children, sizeof(vfs_dir_entry_t) * SIMPLEFS_DIRECTORY_CAPACITY);
    directory->vfs.child_count = 0u;

    if (directory->disk.child_count != 0u) {
        zero_bytes(entries, sizeof(entries));
        if (simplefs_read_dir_entries(directory, entries, directory->disk.child_count) != 0) {
            return -1;
        }

        for (index = 0u; index < directory->disk.child_count; index++) {
            simplefs_runtime_inode_t *child;
            uint32_t child_name_length;

            if (entries[index].inode_index >= mounted_inode_count) {
                return -1;
            }

            child = &runtime_inodes[entries[index].inode_index];
            child_name_length = string_length(entries[index].name);
            if (child_name_length == 0u || child_name_length > SIMPLEFS_NAME_MAX) {
                return -1;
            }

            simplefs_set_inode_name(child, entries[index].name);
            directory->vfs.children[directory->vfs.child_count].name = child->vfs.name;
            directory->vfs.children[directory->vfs.child_count].inode = &child->vfs;
            directory->vfs.child_count++;

            if (child->disk.kind == SIMPLEFS_INODE_DIR && child->populated == 0u && simplefs_populate_directory(child) != 0) {
                return -1;
            }
        }
    }

    directory->populated = 1u;
    return 0;
}

static int32_t simplefs_find_child(simplefs_runtime_inode_t *directory, const char *name) {
    uint32_t index;

    if (simplefs_populate_directory(directory) != 0) {
        return -1;
    }

    for (index = 0u; index < directory->vfs.child_count; index++) {
        if (string_equals(directory->vfs.children[index].name, name)) {
            return (int32_t)index;
        }
    }

    return -1;
}

static int32_t simplefs_alloc_inode(void) {
    uint32_t inode_index;

    for (inode_index = 0u; inode_index < mounted_inode_count; inode_index++) {
        if (!simplefs_inode_used(inode_index)) {
            simplefs_set_inode_used(inode_index, 1);
            return (int32_t)inode_index;
        }
    }

    return -1;
}

static void simplefs_reset_runtime_inode(simplefs_runtime_inode_t *node, uint32_t inode_index) {
    zero_bytes(&node->disk, sizeof(node->disk));
    node->inode_index = inode_index;
    node->populated = 0u;
    node->dir_slot_base = SIMPLEFS_INVALID_DIR_SLOT;
    zero_bytes(node->name, sizeof(node->name));
    node->vfs.name = node->name;
    node->vfs.kind = VFS_INODE_NONE;
    node->vfs.ops = 0;
    node->vfs.inode_ops = &simplefs_inode_ops;
    node->vfs.ref_count = 0u;
    node->vfs.data = 0;
    node->vfs.size = 0u;
    node->vfs.children = 0;
    node->vfs.child_count = 0u;
    node->vfs.child_capacity = 0u;
    node->vfs.private_data = node;
}

static int32_t simplefs_ensure_extent(simplefs_runtime_inode_t *node, uint32_t required_blocks) {
    uint32_t new_start_block;

    if (required_blocks <= node->disk.block_count) {
        return 0;
    }

    if (node->disk.block_count == 0u) {
        if (simplefs_allocate_extent(required_blocks, &new_start_block) != 0) {
            return -1;
        }

        node->disk.data_block = new_start_block;
        node->disk.block_count = required_blocks;
        while (required_blocks-- != 0u) {
            if (simplefs_zero_block(node->disk.data_block + required_blocks) != 0) {
                return -1;
            }
        }
        return 0;
    }

    if (simplefs_extent_is_free(node->disk.data_block + node->disk.block_count, required_blocks - node->disk.block_count)) {
        uint32_t tail_start = node->disk.data_block + node->disk.block_count;
        uint32_t tail_blocks = required_blocks - node->disk.block_count;
        uint32_t index;

        simplefs_mark_extent(tail_start, tail_blocks, 1);
        for (index = 0u; index < tail_blocks; index++) {
            if (simplefs_zero_block(tail_start + index) != 0) {
                return -1;
            }
        }

        node->disk.block_count = required_blocks;
        return 0;
    }

    {
        uint32_t old_start_block = node->disk.data_block;
        uint32_t old_block_count = node->disk.block_count;
        uint32_t index;

        if (simplefs_allocate_extent(required_blocks, &new_start_block) != 0) {
            return -1;
        }

        if (simplefs_copy_extent(old_start_block, new_start_block, old_block_count) != 0) {
            simplefs_mark_extent(new_start_block, required_blocks, 0);
            return -1;
        }

        for (index = old_block_count; index < required_blocks; index++) {
            if (simplefs_zero_block(new_start_block + index) != 0) {
                simplefs_mark_extent(new_start_block, required_blocks, 0);
                return -1;
            }
        }

        simplefs_mark_extent(old_start_block, old_block_count, 0);
        node->disk.data_block = new_start_block;
        node->disk.block_count = required_blocks;
    }

    return 0;
}

static int32_t simplefs_zero_range(simplefs_runtime_inode_t *node, uint32_t start_offset, uint32_t length) {
    uint32_t cleared = 0u;

    while (cleared < length) {
        uint32_t file_offset = start_offset + cleared;
        uint32_t block_offset = file_offset % SIMPLEFS_BLOCK_SIZE;
        uint32_t file_block = file_offset / SIMPLEFS_BLOCK_SIZE;
        uint32_t chunk = min_u32(length - cleared, SIMPLEFS_BLOCK_SIZE - block_offset);
        uint32_t index;

        if (simplefs_read_blocks(node->disk.data_block + file_block, 1u, block_scratch) != 0) {
            return -1;
        }

        for (index = 0u; index < chunk; index++) {
            block_scratch[block_offset + index] = 0u;
        }

        if (simplefs_write_blocks(node->disk.data_block + file_block, 1u, block_scratch) != 0) {
            return -1;
        }

        cleared += chunk;
    }

    return 0;
}

static int32_t simplefs_resize_node(simplefs_runtime_inode_t *node, uint32_t new_size) {
    uint32_t old_size = node->disk.size;
    uint32_t old_blocks = node->disk.block_count;
    uint32_t required_blocks = simplefs_required_blocks(new_size);

    if (mounted_device == 0 || mounted_device->read_only != 0u || node->disk.kind != SIMPLEFS_INODE_FILE) {
        return -1;
    }

    if (required_blocks > old_blocks) {
        if (simplefs_ensure_extent(node, required_blocks) != 0) {
            return -1;
        }

        if (new_size > old_size && simplefs_zero_range(node, old_size, new_size - old_size) != 0) {
            return -1;
        }
    } else if (required_blocks < old_blocks) {
        if (required_blocks == 0u) {
            simplefs_release_extent(node);
        } else {
            simplefs_mark_extent(node->disk.data_block + required_blocks, old_blocks - required_blocks, 0);
            node->disk.block_count = required_blocks;
            if ((new_size % SIMPLEFS_BLOCK_SIZE) != 0u && simplefs_zero_range(node, new_size, SIMPLEFS_BLOCK_SIZE - (new_size % SIMPLEFS_BLOCK_SIZE)) != 0) {
                return -1;
            }
        }
    }

    node->disk.size = new_size;
    node->vfs.size = new_size;
    return simplefs_flush_metadata();
}

static int32_t simplefs_resize_inode(vfs_inode_t *inode, uint32_t size) {
    simplefs_runtime_inode_t *node;

    if (inode == 0 || inode->private_data == 0) {
        return -1;
    }

    node = (simplefs_runtime_inode_t *)inode->private_data;
    return simplefs_resize_node(node, size);
}

static int32_t simplefs_regular_read(vfs_file_t *file, void *buffer, uint32_t length) {
    simplefs_runtime_inode_t *node;
    uint8_t *destination = (uint8_t *)buffer;
    uint32_t copied = 0u;

    if (file == 0 || file->inode == 0 || buffer == 0) {
        return -1;
    }

    node = (simplefs_runtime_inode_t *)file->inode->private_data;
    if (node == 0) {
        return -1;
    }

    if (file->offset >= node->disk.size) {
        return 0;
    }

    if (length > (node->disk.size - file->offset)) {
        length = node->disk.size - file->offset;
    }

    while (copied < length) {
        uint32_t file_offset = file->offset + copied;
        uint32_t block_offset = file_offset % SIMPLEFS_BLOCK_SIZE;
        uint32_t file_block = file_offset / SIMPLEFS_BLOCK_SIZE;
        uint32_t disk_block = node->disk.data_block + file_block;
        uint32_t chunk = min_u32(length - copied, SIMPLEFS_BLOCK_SIZE - block_offset);

        if (file_block >= node->disk.block_count) {
            return copied != 0u ? (int32_t)copied : -1;
        }

        if (block_offset == 0u && chunk == SIMPLEFS_BLOCK_SIZE) {
            if (simplefs_read_blocks(disk_block, 1u, &destination[copied]) != 0) {
                return copied != 0u ? (int32_t)copied : -1;
            }
        } else {
            uint32_t index;

            if (simplefs_read_blocks(disk_block, 1u, block_scratch) != 0) {
                return copied != 0u ? (int32_t)copied : -1;
            }

            for (index = 0u; index < chunk; index++) {
                destination[copied + index] = block_scratch[block_offset + index];
            }
        }

        copied += chunk;
    }

    file->offset += copied;
    return (int32_t)copied;
}

static int32_t simplefs_regular_write(vfs_file_t *file, const char *buffer, uint32_t length) {
    simplefs_runtime_inode_t *node;
    uint32_t written = 0u;
    uint32_t end_offset;

    if (file == 0 || file->inode == 0 || buffer == 0 || mounted_device == 0 || mounted_device->read_only != 0u) {
        return -1;
    }

    node = (simplefs_runtime_inode_t *)file->inode->private_data;
    if (node == 0 || node->disk.kind != SIMPLEFS_INODE_FILE) {
        return -1;
    }

    if (length == 0u) {
        return 0;
    }

    end_offset = file->offset + length;
    if (end_offset < file->offset) {
        return -1;
    }

    if (end_offset > node->disk.size && simplefs_resize_node(node, end_offset) != 0) {
        return -1;
    }

    while (written < length) {
        uint32_t file_offset = file->offset + written;
        uint32_t block_offset = file_offset % SIMPLEFS_BLOCK_SIZE;
        uint32_t file_block = file_offset / SIMPLEFS_BLOCK_SIZE;
        uint32_t disk_block = node->disk.data_block + file_block;
        uint32_t chunk = min_u32(length - written, SIMPLEFS_BLOCK_SIZE - block_offset);
        uint32_t index;

        if (block_offset == 0u && chunk == SIMPLEFS_BLOCK_SIZE) {
            if (simplefs_write_blocks(disk_block, 1u, &buffer[written]) != 0) {
                return written != 0u ? (int32_t)written : -1;
            }
        } else {
            if (simplefs_read_blocks(disk_block, 1u, block_scratch) != 0) {
                return written != 0u ? (int32_t)written : -1;
            }

            for (index = 0u; index < chunk; index++) {
                block_scratch[block_offset + index] = (uint8_t)buffer[written + index];
            }

            if (simplefs_write_blocks(disk_block, 1u, block_scratch) != 0) {
                return written != 0u ? (int32_t)written : -1;
            }
        }

        written += chunk;
    }

    file->offset += written;
    return (int32_t)written;
}

static int32_t simplefs_create_child(vfs_inode_t *directory_inode, const char *name, vfs_inode_kind_t kind, vfs_inode_t **out_inode) {
    simplefs_runtime_inode_t *directory;
    simplefs_runtime_inode_t *child;
    simplefs_dir_entry_disk_t entries[SIMPLEFS_DIRECTORY_CAPACITY];
    int32_t child_inode_index;

    if (directory_inode == 0 || name == 0 || out_inode == 0) {
        return -1;
    }

    directory = (simplefs_runtime_inode_t *)directory_inode->private_data;
    if (directory == 0 || directory->disk.kind != SIMPLEFS_INODE_DIR || mounted_device == 0 || mounted_device->read_only != 0u) {
        return -1;
    }

    if (string_length(name) == 0u || string_length(name) > SIMPLEFS_NAME_MAX || simplefs_find_child(directory, name) >= 0) {
        return -1;
    }

    if (directory->disk.child_count >= SIMPLEFS_DIRECTORY_CAPACITY) {
        return -1;
    }

    zero_bytes(entries, sizeof(entries));
    if (directory->disk.child_count != 0u && simplefs_read_dir_entries(directory, entries, directory->disk.child_count) != 0) {
        return -1;
    }

    child_inode_index = simplefs_alloc_inode();
    if (child_inode_index < 0) {
        return -1;
    }

    child = &runtime_inodes[(uint32_t)child_inode_index];
    simplefs_reset_runtime_inode(child, (uint32_t)child_inode_index);
    child->disk.kind = kind == VFS_INODE_DIR ? SIMPLEFS_INODE_DIR : SIMPLEFS_INODE_FILE;
    child->vfs.kind = kind;
    child->vfs.ops = kind == VFS_INODE_REG ? &simplefs_file_ops : 0;
    child->vfs.inode_ops = &simplefs_inode_ops;
    simplefs_set_inode_name(child, name);

    entries[directory->disk.child_count].inode_index = (uint32_t)child_inode_index;
    copy_bytes(entries[directory->disk.child_count].name, name, string_length(name));

    if (simplefs_write_dir_entries(directory, entries, directory->disk.child_count + 1u) != 0) {
        simplefs_set_inode_used((uint32_t)child_inode_index, 0);
        simplefs_reset_runtime_inode(child, (uint32_t)child_inode_index);
        return -1;
    }

    directory->vfs.children[directory->vfs.child_count].name = child->vfs.name;
    directory->vfs.children[directory->vfs.child_count].inode = &child->vfs;
    directory->vfs.child_count++;

    if (simplefs_flush_metadata() != 0) {
        return -1;
    }

    *out_inode = &child->vfs;
    return 0;
}

static int32_t simplefs_remove_child(vfs_inode_t *directory_inode, const char *name) {
    simplefs_runtime_inode_t *directory;
    simplefs_dir_entry_disk_t entries[SIMPLEFS_DIRECTORY_CAPACITY];
    int32_t child_entry_index;
    uint32_t child_inode_index;
    simplefs_runtime_inode_t *child;
    uint32_t index;

    if (directory_inode == 0 || name == 0) {
        return -1;
    }

    directory = (simplefs_runtime_inode_t *)directory_inode->private_data;
    if (directory == 0 || directory->disk.kind != SIMPLEFS_INODE_DIR || mounted_device == 0 || mounted_device->read_only != 0u) {
        return -1;
    }

    if (simplefs_populate_directory(directory) != 0) {
        return -1;
    }

    child_entry_index = simplefs_find_child(directory, name);
    if (child_entry_index < 0) {
        return -1;
    }

    if ((uint32_t)child_entry_index >= directory->disk.child_count) {
        return -1;
    }

    zero_bytes(entries, sizeof(entries));
    if (directory->disk.child_count != 0u && simplefs_read_dir_entries(directory, entries, directory->disk.child_count) != 0) {
        return -1;
    }

    child_inode_index = entries[(uint32_t)child_entry_index].inode_index;
    if (child_inode_index >= mounted_inode_count || !simplefs_inode_used(child_inode_index)) {
        return -1;
    }

    child = &runtime_inodes[child_inode_index];
    if (child->vfs.ref_count != 0u) {
        return -1;
    }

    if (child->disk.kind == SIMPLEFS_INODE_DIR) {
        if (simplefs_populate_directory(child) != 0) {
            return -1;
        }

        if (child->vfs.child_count != 0u) {
            return -1;
        }
    }

    simplefs_release_extent(child);
    child->disk.size = 0u;
    child->disk.child_count = 0u;
    child->vfs.size = 0u;

    for (index = (uint32_t)child_entry_index; (index + 1u) < directory->disk.child_count; index++) {
        entries[index] = entries[index + 1u];
        directory->vfs.children[index] = directory->vfs.children[index + 1u];
    }

    if (directory->disk.child_count != 0u) {
        uint32_t last_index = directory->disk.child_count - 1u;

        zero_bytes(&entries[last_index], sizeof(entries[last_index]));
        directory->vfs.children[last_index].name = 0;
        directory->vfs.children[last_index].inode = 0;
    }

    if (simplefs_write_dir_entries(directory, entries, directory->disk.child_count - 1u) != 0) {
        return -1;
    }

    if (directory->vfs.child_count != 0u) {
        directory->vfs.child_count--;
    }

    simplefs_set_inode_used(child_inode_index, 0);
    simplefs_reset_runtime_inode(child, child_inode_index);

    if (simplefs_flush_metadata() != 0) {
        return -1;
    }

    return 0;
}

int32_t simplefs_mount(block_device_t *device, vfs_inode_t **out_root) {
    simplefs_inode_disk_t disk_inodes[SIMPLEFS_MAX_MOUNT_INODES];
    uint32_t inode_table_bytes;
    uint32_t inode_table_blocks;
    uint32_t block_bitmap_bytes;
    uint32_t index;
    simplefs_runtime_inode_t *root;

    if (device == 0 || out_root == 0) {
        return -1;
    }

    if (device->block_size != SIMPLEFS_BLOCK_SIZE) {
        return -1;
    }

    zero_bytes(block_scratch, sizeof(block_scratch));
    if (block_read(device, 0u, 1u, block_scratch) != 0) {
        return -1;
    }

    copy_bytes(&mounted_superblock, block_scratch, sizeof(mounted_superblock));

    if (mounted_superblock.magic != SIMPLEFS_MAGIC || mounted_superblock.version != SIMPLEFS_VERSION || mounted_superblock.block_size != SIMPLEFS_BLOCK_SIZE) {
        return -1;
    }

    if (mounted_superblock.total_blocks == 0u || mounted_superblock.total_blocks > device->block_count || mounted_superblock.total_blocks > SIMPLEFS_MAX_MOUNT_BLOCKS) {
        return -1;
    }

    if (mounted_superblock.inode_count == 0u || mounted_superblock.inode_count > SIMPLEFS_MAX_MOUNT_INODES || mounted_superblock.root_inode >= mounted_superblock.inode_count) {
        return -1;
    }

    inode_table_bytes = mounted_superblock.inode_count * sizeof(simplefs_inode_disk_t);
    inode_table_blocks = align_up(inode_table_bytes, SIMPLEFS_BLOCK_SIZE) / SIMPLEFS_BLOCK_SIZE;
    if (inode_table_blocks != mounted_superblock.inode_table_blocks || mounted_superblock.inode_bitmap_blocks != 1u) {
        return -1;
    }

    block_bitmap_bytes = align_up(mounted_superblock.total_blocks, 8u) / 8u;
    if (mounted_superblock.block_bitmap_blocks != (align_up(block_bitmap_bytes, SIMPLEFS_BLOCK_SIZE) / SIMPLEFS_BLOCK_SIZE)) {
        return -1;
    }

    zero_bytes(disk_inodes, sizeof(disk_inodes));
    if (block_read(device, mounted_superblock.inode_table_start, inode_table_blocks, disk_inodes) != 0) {
        return -1;
    }

    zero_bytes(inode_bitmap, sizeof(inode_bitmap));
    if (block_read(device, mounted_superblock.inode_bitmap_start, mounted_superblock.inode_bitmap_blocks, inode_bitmap) != 0) {
        return -1;
    }

    zero_bytes(block_bitmap, sizeof(block_bitmap));
    if (block_read(device, mounted_superblock.block_bitmap_start, mounted_superblock.block_bitmap_blocks, block_bitmap) != 0) {
        return -1;
    }

    mounted_device = device;
    mounted_inode_count = mounted_superblock.inode_count;
    next_dir_entry_slot = 0u;
    zero_bytes(runtime_inodes, sizeof(runtime_inodes));
    zero_bytes(runtime_dir_entries, sizeof(runtime_dir_entries));

    for (index = 0u; index < mounted_inode_count; index++) {
        simplefs_runtime_inode_t *node = &runtime_inodes[index];

        simplefs_reset_runtime_inode(node, index);
        node->disk = disk_inodes[index];
        node->vfs.size = node->disk.size;

        if (!simplefs_inode_used(index)) {
            continue;
        }

        if (node->disk.kind == SIMPLEFS_INODE_DIR) {
            node->vfs.kind = VFS_INODE_DIR;
            node->vfs.ops = 0;
            node->vfs.inode_ops = &simplefs_inode_ops;
        } else if (node->disk.kind == SIMPLEFS_INODE_FILE) {
            node->vfs.kind = VFS_INODE_REG;
            node->vfs.ops = &simplefs_file_ops;
            node->vfs.inode_ops = &simplefs_inode_ops;
        } else {
            return -1;
        }
    }

    root = &runtime_inodes[mounted_superblock.root_inode];
    root->name[0] = '/';
    root->name[1] = '\0';
    root->vfs.name = root->name;

    if (simplefs_populate_directory(root) != 0) {
        return -1;
    }

    *out_root = &root->vfs;

    console_write("simplefs: mounted ");
    console_write(device->name);
    console_write(" inodes 0x");
    console_write_hex32(mounted_inode_count);
    console_write("\n");
    return 0;
}
