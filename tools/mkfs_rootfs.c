#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "shared/simplefs_format.h"

#define ROOTFS_TOTAL_INODES 64u
#define ROOTFS_ROOT_INODE 0u
#define ROOTFS_BIN_INODE 1u
#define ROOTFS_DEV_INODE 2u
#define ROOTFS_VAR_INODE 3u
#define ROOTFS_ETC_INODE 4u
#define ROOTFS_TMP_INODE 5u
#define ROOTFS_FIRST_FILE_INODE 6u
#define ROOTFS_ROOT_CHILD_COUNT 5u
#define ROOTFS_MAX_BIN_FILES (ROOTFS_TOTAL_INODES - ROOTFS_FIRST_FILE_INODE)
#define ROOTFS_DEFAULT_TOTAL_BLOCKS 32768u

typedef struct rootfs_input_file {
    const char *host_path;
    const char *image_path;
    const char *name;
    uint32_t parent_inode_index;
    uint8_t *bytes;
    uint32_t length;
    uint32_t inode_index;
    uint32_t data_block;
    uint32_t block_count;
} rootfs_input_file_t;

static uint32_t align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
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

static uint32_t string_length(const char *text) {
    uint32_t length = 0u;

    while (text[length] != '\0') {
        length++;
    }

    return length;
}

static int strings_equal(const char *left, const char *right) {
    uint32_t index = 0u;

    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }

        index++;
    }

    return left[index] == right[index];
}

static uint8_t *load_file(const char *path, uint32_t *out_length) {
    FILE *file;
    long length;
    uint8_t *data;

    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    length = ftell(file);
    if (length < 0) {
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    data = (uint8_t *)malloc((size_t)length);
    if (data == NULL) {
        fclose(file);
        return NULL;
    }

    if ((long)fread(data, 1u, (size_t)length, file) != length) {
        free(data);
        fclose(file);
        return NULL;
    }

    fclose(file);
    *out_length = (uint32_t)length;
    return data;
}

static int write_image(const char *path, const uint8_t *image, uint32_t image_length) {
    FILE *file = fopen(path, "wb");

    if (file == NULL) {
        return -1;
    }

    if (fwrite(image, 1u, image_length, file) != image_length) {
        fclose(file);
        return -1;
    }

    fclose(file);
    return 0;
}

static void bitmap_mark(uint8_t *bitmap, uint32_t index) {
    bitmap[index / 8u] |= (uint8_t)(1u << (index % 8u));
}

static int parse_total_blocks(const char *text, uint32_t *out_total_blocks) {
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 0);

    if (text == end || end == NULL || *end != '\0' || value == 0ul || value > (unsigned long)UINT32_MAX) {
        return -1;
    }

    *out_total_blocks = (uint32_t)value;
    return 0;
}

static const char *rootfs_image_leaf(const char *path, uint32_t *out_parent_inode) {
    static const char bin_prefix[] = "/bin/";
    static const char etc_prefix[] = "/etc/";
    const char *prefix = NULL;
    uint32_t prefix_length = 0u;
    uint32_t index;

    if (path == NULL || out_parent_inode == NULL) {
        return NULL;
    }

    if (path[0] == '/' && path[1] == 'b' && path[2] == 'i' && path[3] == 'n' && path[4] == '/') {
        prefix = bin_prefix;
        prefix_length = (uint32_t)(sizeof(bin_prefix) - 1u);
        *out_parent_inode = ROOTFS_BIN_INODE;
    } else if (path[0] == '/' && path[1] == 'e' && path[2] == 't' && path[3] == 'c' && path[4] == '/') {
        prefix = etc_prefix;
        prefix_length = (uint32_t)(sizeof(etc_prefix) - 1u);
        *out_parent_inode = ROOTFS_ETC_INODE;
    } else {
        return NULL;
    }

    for (index = 0u; index < prefix_length; index++) {
        if (path[index] != prefix[index]) {
            return NULL;
        }
    }

    if (path[prefix_length] == '\0') {
        return NULL;
    }

    for (index = prefix_length; path[index] != '\0'; index++) {
        if (path[index] == '/' || (index - prefix_length) >= SIMPLEFS_NAME_MAX) {
            return NULL;
        }
    }

    return &path[prefix_length];
}

static void free_input_files(rootfs_input_file_t *files, uint32_t file_count) {
    uint32_t index;

    if (files == NULL) {
        return;
    }

    for (index = 0u; index < file_count; index++) {
        free(files[index].bytes);
    }

    free(files);
}

static void print_usage(const char *program_name) {
    fprintf(stderr, "usage: %s <rootfs.img> <init.elf> [total_blocks]\n", program_name);
    fprintf(stderr, "   or: %s <rootfs.img> <total_blocks> <host_file> <image_path> [<host_file> <image_path> ...]\n", program_name);
}

int main(int argc, char **argv) {
    simplefs_superblock_t superblock;
    simplefs_inode_disk_t inodes[ROOTFS_TOTAL_INODES];
    simplefs_dir_entry_disk_t root_entries[ROOTFS_ROOT_CHILD_COUNT];
    simplefs_dir_entry_disk_t *bin_entries = NULL;
    simplefs_dir_entry_disk_t *etc_entries = NULL;
    rootfs_input_file_t *files = NULL;
    uint8_t *image = NULL;
    uint8_t *inode_bitmap;
    uint8_t *block_bitmap;
    uint32_t total_blocks = ROOTFS_DEFAULT_TOTAL_BLOCKS;
    uint32_t file_count;
    uint32_t bin_file_count = 0u;
    uint32_t etc_file_count = 0u;
    uint32_t used_inode_count;
    uint32_t inode_table_blocks;
    uint32_t inode_bitmap_blocks;
    uint32_t block_bitmap_bytes;
    uint32_t block_bitmap_blocks;
    uint32_t root_dir_block;
    uint32_t bin_dir_block;
    uint32_t bin_dir_blocks;
    uint32_t etc_dir_block;
    uint32_t etc_dir_blocks;
    uint32_t next_data_block;
    uint32_t image_length;
    uint32_t index;
    uint32_t argument_index;
    int legacy_mode;
    int exit_code = 1;

    legacy_mode = (argc == 3 || argc == 4);
    if (!legacy_mode && (argc < 5 || ((argc - 3) % 2) != 0)) {
        print_usage(argv[0]);
        return 1;
    }

    if (legacy_mode) {
        if (argc == 4 && parse_total_blocks(argv[3], &total_blocks) != 0) {
            print_usage(argv[0]);
            return 1;
        }

        file_count = 1u;
        files = (rootfs_input_file_t *)calloc(file_count, sizeof(*files));
        if (files == NULL) {
            return 1;
        }

        files[0].host_path = argv[2];
        files[0].image_path = "/bin/init";
        files[0].name = "init";
        files[0].parent_inode_index = ROOTFS_BIN_INODE;
    } else {
        if (parse_total_blocks(argv[2], &total_blocks) != 0) {
            print_usage(argv[0]);
            return 1;
        }

        file_count = (uint32_t)((argc - 3) / 2);
        if (file_count == 0u || file_count > ROOTFS_MAX_BIN_FILES) {
            fprintf(stderr, "unsupported /bin file count %u\n", file_count);
            return 1;
        }

        files = (rootfs_input_file_t *)calloc(file_count, sizeof(*files));
        if (files == NULL) {
            return 1;
        }

        argument_index = 3u;
        for (index = 0u; index < file_count; index++) {
            const char *name;

            files[index].host_path = argv[argument_index++];
            files[index].image_path = argv[argument_index++];
            name = rootfs_image_leaf(files[index].image_path, &files[index].parent_inode_index);
            if (name == NULL) {
                fprintf(stderr, "only /bin/<name> or /etc/<name> images are supported: %s\n", files[index].image_path);
                goto cleanup;
            }

            files[index].name = name;
        }
    }

    for (index = 0u; index < file_count; index++) {
        uint32_t other_index;

        for (other_index = 0u; other_index < index; other_index++) {
            if (files[index].parent_inode_index == files[other_index].parent_inode_index &&
                strings_equal(files[index].name, files[other_index].name)) {
                fprintf(stderr, "duplicate rootfs entry: %s\n", files[index].name);
                goto cleanup;
            }
        }
    }

    for (index = 0u; index < file_count; index++) {
        files[index].bytes = load_file(files[index].host_path, &files[index].length);
        if (files[index].bytes == NULL) {
            fprintf(stderr, "failed to read %s\n", files[index].host_path);
            goto cleanup;
        }

        files[index].inode_index = ROOTFS_FIRST_FILE_INODE + index;
        files[index].block_count = align_up(files[index].length, SIMPLEFS_BLOCK_SIZE) / SIMPLEFS_BLOCK_SIZE;
        if (files[index].parent_inode_index == ROOTFS_BIN_INODE) {
            bin_file_count++;
        } else if (files[index].parent_inode_index == ROOTFS_ETC_INODE) {
            etc_file_count++;
        }
    }

    used_inode_count = ROOTFS_FIRST_FILE_INODE + file_count;
    inode_table_blocks = align_up(ROOTFS_TOTAL_INODES * sizeof(simplefs_inode_disk_t), SIMPLEFS_BLOCK_SIZE) / SIMPLEFS_BLOCK_SIZE;
    inode_bitmap_blocks = 1u;
    block_bitmap_bytes = align_up(total_blocks, 8u) / 8u;
    block_bitmap_blocks = align_up(block_bitmap_bytes, SIMPLEFS_BLOCK_SIZE) / SIMPLEFS_BLOCK_SIZE;
    bin_dir_blocks = bin_file_count == 0u ? 0u :
        align_up(bin_file_count * sizeof(simplefs_dir_entry_disk_t), SIMPLEFS_BLOCK_SIZE) / SIMPLEFS_BLOCK_SIZE;
    etc_dir_blocks = etc_file_count == 0u ? 0u :
        align_up(etc_file_count * sizeof(simplefs_dir_entry_disk_t), SIMPLEFS_BLOCK_SIZE) / SIMPLEFS_BLOCK_SIZE;

    zero_bytes(&superblock, sizeof(superblock));
    superblock.magic = SIMPLEFS_MAGIC;
    superblock.version = SIMPLEFS_VERSION;
    superblock.block_size = SIMPLEFS_BLOCK_SIZE;
    superblock.total_blocks = total_blocks;
    superblock.inode_count = ROOTFS_TOTAL_INODES;
    superblock.inode_table_start = 1u;
    superblock.inode_table_blocks = inode_table_blocks;
    superblock.inode_bitmap_start = superblock.inode_table_start + inode_table_blocks;
    superblock.inode_bitmap_blocks = inode_bitmap_blocks;
    superblock.block_bitmap_start = superblock.inode_bitmap_start + inode_bitmap_blocks;
    superblock.block_bitmap_blocks = block_bitmap_blocks;
    superblock.data_block_start = superblock.block_bitmap_start + block_bitmap_blocks;
    superblock.root_inode = ROOTFS_ROOT_INODE;

    root_dir_block = superblock.data_block_start;
    bin_dir_block = root_dir_block + 1u;
    etc_dir_block = bin_dir_block + bin_dir_blocks;
    next_data_block = etc_dir_block + etc_dir_blocks;

    for (index = 0u; index < file_count; index++) {
        files[index].data_block = next_data_block;
        next_data_block += files[index].block_count;
    }

    if (next_data_block > total_blocks) {
        fprintf(stderr, "rootfs too small for /bin payloads\n");
        goto cleanup;
    }

    image_length = total_blocks * SIMPLEFS_BLOCK_SIZE;
    image = (uint8_t *)malloc(image_length);
    if (image == NULL) {
        goto cleanup;
    }

    bin_entries = (simplefs_dir_entry_disk_t *)calloc(file_count, sizeof(*bin_entries));
    if (bin_entries == NULL) {
        goto cleanup;
    }

    etc_entries = (simplefs_dir_entry_disk_t *)calloc(file_count, sizeof(*etc_entries));
    if (etc_entries == NULL) {
        goto cleanup;
    }

    zero_bytes(image, image_length);
    zero_bytes(inodes, sizeof(inodes));
    zero_bytes(root_entries, sizeof(root_entries));

    inodes[ROOTFS_ROOT_INODE].kind = SIMPLEFS_INODE_DIR;
    inodes[ROOTFS_ROOT_INODE].size = sizeof(root_entries);
    inodes[ROOTFS_ROOT_INODE].data_block = root_dir_block;
    inodes[ROOTFS_ROOT_INODE].block_count = 1u;
    inodes[ROOTFS_ROOT_INODE].child_count = ROOTFS_ROOT_CHILD_COUNT;
    inodes[ROOTFS_ROOT_INODE].mode = 0755u;
    inodes[ROOTFS_ROOT_INODE].uid = 0u;
    inodes[ROOTFS_ROOT_INODE].gid = 0u;

    inodes[ROOTFS_BIN_INODE].kind = SIMPLEFS_INODE_DIR;
    inodes[ROOTFS_BIN_INODE].size = bin_file_count * sizeof(simplefs_dir_entry_disk_t);
    inodes[ROOTFS_BIN_INODE].data_block = bin_dir_block;
    inodes[ROOTFS_BIN_INODE].block_count = bin_dir_blocks;
    inodes[ROOTFS_BIN_INODE].child_count = bin_file_count;
    inodes[ROOTFS_BIN_INODE].mode = 0755u;
    inodes[ROOTFS_BIN_INODE].uid = 0u;
    inodes[ROOTFS_BIN_INODE].gid = 0u;

    inodes[ROOTFS_DEV_INODE].kind = SIMPLEFS_INODE_DIR;
    inodes[ROOTFS_DEV_INODE].mode = 0755u;
    inodes[ROOTFS_DEV_INODE].uid = 0u;
    inodes[ROOTFS_DEV_INODE].gid = 0u;
    inodes[ROOTFS_VAR_INODE].kind = SIMPLEFS_INODE_DIR;
    inodes[ROOTFS_VAR_INODE].mode = 0755u;
    inodes[ROOTFS_VAR_INODE].uid = 0u;
    inodes[ROOTFS_VAR_INODE].gid = 0u;
    inodes[ROOTFS_ETC_INODE].kind = SIMPLEFS_INODE_DIR;
    inodes[ROOTFS_ETC_INODE].size = etc_file_count * sizeof(simplefs_dir_entry_disk_t);
    inodes[ROOTFS_ETC_INODE].data_block = etc_dir_blocks == 0u ? 0u : etc_dir_block;
    inodes[ROOTFS_ETC_INODE].block_count = etc_dir_blocks;
    inodes[ROOTFS_ETC_INODE].child_count = etc_file_count;
    inodes[ROOTFS_ETC_INODE].mode = 0755u;
    inodes[ROOTFS_ETC_INODE].uid = 0u;
    inodes[ROOTFS_ETC_INODE].gid = 0u;
    inodes[ROOTFS_TMP_INODE].kind = SIMPLEFS_INODE_DIR;
    inodes[ROOTFS_TMP_INODE].mode = 01777u;
    inodes[ROOTFS_TMP_INODE].uid = 0u;
    inodes[ROOTFS_TMP_INODE].gid = 0u;

    root_entries[0].inode_index = ROOTFS_BIN_INODE;
    copy_bytes(root_entries[0].name, "bin", 3u);
    root_entries[1].inode_index = ROOTFS_DEV_INODE;
    copy_bytes(root_entries[1].name, "dev", 3u);
    root_entries[2].inode_index = ROOTFS_VAR_INODE;
    copy_bytes(root_entries[2].name, "var", 3u);
    root_entries[3].inode_index = ROOTFS_ETC_INODE;
    copy_bytes(root_entries[3].name, "etc", 3u);
    root_entries[4].inode_index = ROOTFS_TMP_INODE;
    copy_bytes(root_entries[4].name, "tmp", 3u);

    bin_file_count = 0u;
    etc_file_count = 0u;
    for (index = 0u; index < file_count; index++) {
        const uint32_t name_length = string_length(files[index].name);

        inodes[files[index].inode_index].kind = SIMPLEFS_INODE_FILE;
        inodes[files[index].inode_index].size = files[index].length;
        inodes[files[index].inode_index].data_block = files[index].data_block;
        inodes[files[index].inode_index].block_count = files[index].block_count;
        inodes[files[index].inode_index].mode = files[index].parent_inode_index == ROOTFS_BIN_INODE ? 0755u : 0644u;
        inodes[files[index].inode_index].uid = 0u;
        inodes[files[index].inode_index].gid = 0u;

        if (files[index].parent_inode_index == ROOTFS_BIN_INODE) {
            bin_entries[bin_file_count].inode_index = files[index].inode_index;
            copy_bytes(bin_entries[bin_file_count].name, files[index].name, name_length);
            bin_file_count++;
        } else {
            etc_entries[etc_file_count].inode_index = files[index].inode_index;
            copy_bytes(etc_entries[etc_file_count].name, files[index].name, name_length);
            etc_file_count++;
        }
    }

    inode_bitmap = &image[superblock.inode_bitmap_start * SIMPLEFS_BLOCK_SIZE];
    block_bitmap = &image[superblock.block_bitmap_start * SIMPLEFS_BLOCK_SIZE];

    for (index = 0u; index < used_inode_count; index++) {
        bitmap_mark(inode_bitmap, index);
    }

    for (index = 0u; index < superblock.data_block_start; index++) {
        bitmap_mark(block_bitmap, index);
    }

    bitmap_mark(block_bitmap, root_dir_block);
    for (index = 0u; index < bin_dir_blocks; index++) {
        bitmap_mark(block_bitmap, bin_dir_block + index);
    }
    for (index = 0u; index < etc_dir_blocks; index++) {
        bitmap_mark(block_bitmap, etc_dir_block + index);
    }

    for (index = 0u; index < file_count; index++) {
        uint32_t block_index;

        for (block_index = 0u; block_index < files[index].block_count; block_index++) {
            bitmap_mark(block_bitmap, files[index].data_block + block_index);
        }
    }

    copy_bytes(image, &superblock, sizeof(superblock));
    copy_bytes(&image[superblock.inode_table_start * SIMPLEFS_BLOCK_SIZE], inodes, sizeof(inodes));
    copy_bytes(&image[root_dir_block * SIMPLEFS_BLOCK_SIZE], root_entries, sizeof(root_entries));
    if (bin_dir_blocks != 0u) {
        copy_bytes(&image[bin_dir_block * SIMPLEFS_BLOCK_SIZE], bin_entries, bin_file_count * sizeof(*bin_entries));
    }
    if (etc_dir_blocks != 0u) {
        copy_bytes(&image[etc_dir_block * SIMPLEFS_BLOCK_SIZE], etc_entries, etc_file_count * sizeof(*etc_entries));
    }

    for (index = 0u; index < file_count; index++) {
        copy_bytes(&image[files[index].data_block * SIMPLEFS_BLOCK_SIZE], files[index].bytes, files[index].length);
    }

    if (write_image(argv[1], image, image_length) != 0) {
        fprintf(stderr, "failed to write %s\n", argv[1]);
        goto cleanup;
    }

    exit_code = 0;

cleanup:
    free(etc_entries);
    free(bin_entries);
    free(image);
    free_input_files(files, file_count);
    return exit_code;
}
