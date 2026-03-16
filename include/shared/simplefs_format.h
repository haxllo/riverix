#ifndef RIVERIX_SIMPLEFS_FORMAT_H
#define RIVERIX_SIMPLEFS_FORMAT_H

#include <stdint.h>

#define SIMPLEFS_MAGIC 0x31534653u
#define SIMPLEFS_VERSION 2u
#define SIMPLEFS_BLOCK_SIZE 512u
#define SIMPLEFS_NAME_MAX 28u

typedef enum simplefs_inode_kind {
    SIMPLEFS_INODE_NONE = 0u,
    SIMPLEFS_INODE_DIR = 1u,
    SIMPLEFS_INODE_FILE = 2u,
} simplefs_inode_kind_t;

typedef struct simplefs_superblock {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_count;
    uint32_t inode_table_start;
    uint32_t inode_table_blocks;
    uint32_t inode_bitmap_start;
    uint32_t inode_bitmap_blocks;
    uint32_t block_bitmap_start;
    uint32_t block_bitmap_blocks;
    uint32_t data_block_start;
    uint32_t root_inode;
    uint32_t reserved;
} __attribute__((packed)) simplefs_superblock_t;

typedef struct simplefs_inode_disk {
    uint32_t kind;
    uint32_t size;
    uint32_t data_block;
    uint32_t block_count;
    uint32_t child_count;
    uint32_t reserved[3];
} __attribute__((packed)) simplefs_inode_disk_t;

typedef struct simplefs_dir_entry_disk {
    uint32_t inode_index;
    char name[SIMPLEFS_NAME_MAX];
} __attribute__((packed)) simplefs_dir_entry_disk_t;

#endif
