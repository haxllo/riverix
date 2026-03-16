#include "kernel/partition.h"

#include <stdint.h>

#include "kernel/console.h"

#define GPT_HEADER_BLOCK 1u
#define GPT_SIGNATURE_LOW 0x20494645u
#define GPT_SIGNATURE_HIGH 0x54524150u
#define GPT_ENTRY_NAME_CHARS 36u
#define GPT_MAX_ENTRY_SIZE 512u
#define GPT_MAX_SCAN_ENTRIES 128u

typedef struct gpt_header {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[16];
    uint64_t entry_lba;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t entry_array_crc32;
} __attribute__((packed)) gpt_header_t;

typedef struct gpt_partition_entry {
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint16_t name[GPT_ENTRY_NAME_CHARS];
} __attribute__((packed)) gpt_partition_entry_t;

typedef struct partition_context {
    block_device_t *parent;
    uint32_t start_block;
    uint32_t block_count;
} partition_context_t;

static partition_context_t rootfs_partition_context;
static block_device_t rootfs_partition_device;
static uint8_t entry_block[GPT_MAX_ENTRY_SIZE];

static int guid_is_zero(const uint8_t *guid) {
    uint32_t index;

    for (index = 0u; index < 16u; index++) {
        if (guid[index] != 0u) {
            return 0;
        }
    }

    return 1;
}

static int gpt_entry_name_equals(const gpt_partition_entry_t *entry, const char *ascii_name) {
    uint32_t index = 0u;

    while (ascii_name[index] != '\0') {
        if (index >= GPT_ENTRY_NAME_CHARS) {
            return 0;
        }

        if (entry->name[index] != (uint16_t)(uint8_t)ascii_name[index]) {
            return 0;
        }

        index++;
    }

    return index < GPT_ENTRY_NAME_CHARS && entry->name[index] == 0u;
}

static int32_t partition_read(block_device_t *device, uint32_t block_index, uint32_t block_count, void *buffer) {
    partition_context_t *context = (partition_context_t *)device->context;

    if (context == 0 || context->parent == 0) {
        return -1;
    }

    if (block_index >= context->block_count || block_count > (context->block_count - block_index)) {
        return -1;
    }

    return block_read(context->parent, context->start_block + block_index, block_count, buffer);
}

static int32_t partition_write(block_device_t *device, uint32_t block_index, uint32_t block_count, const void *buffer) {
    partition_context_t *context = (partition_context_t *)device->context;

    if (context == 0 || context->parent == 0) {
        return -1;
    }

    if (block_index >= context->block_count || block_count > (context->block_count - block_index)) {
        return -1;
    }

    return block_write(context->parent, context->start_block + block_index, block_count, buffer);
}

int32_t partition_register_rootfs(block_device_t *disk, const char *device_name) {
    gpt_header_t header;
    uint32_t entry_count;
    uint32_t entry_index;
    uint32_t entries_per_block;
    uint32_t current_entry_block = 0xFFFFFFFFu;

    if (disk == 0 || device_name == 0 || disk->block_size != 512u) {
        return -1;
    }

    if (block_read(disk, GPT_HEADER_BLOCK, 1u, entry_block) != 0) {
        console_write("partition: failed to read gpt header\n");
        return -1;
    }

    {
        uint8_t *header_bytes = (uint8_t *)&header;
        const uint8_t *block_bytes = entry_block;
        uint32_t index;

        for (index = 0u; index < sizeof(header); index++) {
            header_bytes[index] = block_bytes[index];
        }
    }

    if ((uint32_t)header.signature != GPT_SIGNATURE_LOW || (uint32_t)(header.signature >> 32) != GPT_SIGNATURE_HIGH) {
        console_write("partition: missing gpt signature\n");
        return -1;
    }

    if (header.entry_size < sizeof(gpt_partition_entry_t) || header.entry_size > sizeof(entry_block) || (512u % header.entry_size) != 0u) {
        console_write("partition: unsupported gpt entry size\n");
        return -1;
    }

    entries_per_block = 512u / header.entry_size;
    entry_count = header.entry_count;
    if (entry_count > GPT_MAX_SCAN_ENTRIES) {
        entry_count = GPT_MAX_SCAN_ENTRIES;
    }

    for (entry_index = 0u; entry_index < entry_count; entry_index++) {
        uint32_t entry_block_index = entry_index / entries_per_block;
        uint32_t entry_slot = entry_index % entries_per_block;
        const gpt_partition_entry_t *entry;

        if (entry_block_index != current_entry_block) {
            current_entry_block = entry_block_index;
            if (block_read(disk, (uint32_t)header.entry_lba + current_entry_block, 1u, entry_block) != 0) {
                console_write("partition: failed to read gpt entries\n");
                return -1;
            }
        }

        entry = (const gpt_partition_entry_t *)(const void *)&entry_block[entry_slot * header.entry_size];
        if (guid_is_zero(entry->type_guid)) {
            continue;
        }

        if (!gpt_entry_name_equals(entry, "riverix-rootfs")) {
            continue;
        }

        if (entry->last_lba < entry->first_lba || entry->last_lba >= disk->block_count) {
            console_write("partition: invalid rootfs bounds\n");
            return -1;
        }

        rootfs_partition_context.parent = disk;
        rootfs_partition_context.start_block = (uint32_t)entry->first_lba;
        rootfs_partition_context.block_count = (uint32_t)(entry->last_lba - entry->first_lba + 1u);

        rootfs_partition_device.name = device_name;
        rootfs_partition_device.block_size = disk->block_size;
        rootfs_partition_device.block_count = rootfs_partition_context.block_count;
        rootfs_partition_device.read_only = disk->read_only;
        rootfs_partition_device.read = partition_read;
        rootfs_partition_device.write = disk->read_only == 0u ? partition_write : 0;
        rootfs_partition_device.context = &rootfs_partition_context;
        rootfs_partition_device.parent = disk;

        if (block_register(&rootfs_partition_device) != 0) {
            console_write("partition: failed to register ");
            console_write(device_name);
            console_write("\n");
            return -1;
        }

        console_write("partition: registered ");
        console_write(device_name);
        console_write(" start 0x");
        console_write_hex32(rootfs_partition_context.start_block);
        console_write(" blocks 0x");
        console_write_hex32(rootfs_partition_context.block_count);
        console_write("\n");
        return 0;
    }

    console_write("partition: rootfs partition not found\n");
    return -1;
}
