#ifndef RIVERIX_VFS_H
#define RIVERIX_VFS_H

#include <stdint.h>

#include "kernel/multiboot.h"

#define VFS_MAX_FDS 8u
#define VFS_MAX_FILES 16u

typedef struct vfs_inode vfs_inode_t;
typedef struct vfs_file vfs_file_t;
typedef struct vfs_dir_entry vfs_dir_entry_t;
typedef struct vfs_inode_ops vfs_inode_ops_t;

typedef int32_t (*vfs_read_op_t)(vfs_file_t *file, void *buffer, uint32_t length);
typedef int32_t (*vfs_write_op_t)(vfs_file_t *file, const char *buffer, uint32_t length);

typedef enum vfs_inode_kind {
    VFS_INODE_NONE = 0,
    VFS_INODE_DEV,
    VFS_INODE_REG,
    VFS_INODE_DIR,
} vfs_inode_kind_t;

typedef enum vfs_open_flags {
    VFS_O_RDONLY = 1u << 0,
    VFS_O_WRONLY = 1u << 1,
    VFS_O_RDWR = VFS_O_RDONLY | VFS_O_WRONLY,
} vfs_open_flags_t;

typedef int32_t (*vfs_create_child_op_t)(vfs_inode_t *directory, const char *name, vfs_inode_kind_t kind, vfs_inode_t **out_inode);
typedef int32_t (*vfs_resize_op_t)(vfs_inode_t *inode, uint32_t size);

typedef struct vfs_file_ops {
    vfs_read_op_t read;
    vfs_write_op_t write;
} vfs_file_ops_t;

struct vfs_inode_ops {
    vfs_create_child_op_t create_child;
    vfs_resize_op_t resize;
};

struct vfs_dir_entry {
    const char *name;
    vfs_inode_t *inode;
};

struct vfs_inode {
    const char *name;
    vfs_inode_kind_t kind;
    const vfs_file_ops_t *ops;
    const vfs_inode_ops_t *inode_ops;
    uint32_t ref_count;
    const uint8_t *data;
    uint32_t size;
    vfs_dir_entry_t *children;
    uint32_t child_count;
    uint32_t child_capacity;
    void *private_data;
};

struct vfs_file {
    vfs_inode_t *inode;
    uint32_t flags;
    uint32_t offset;
    uint32_t ref_count;
};

void vfs_init(const multiboot_info_t *multiboot_info);
int32_t vfs_open_path(vfs_file_t **out_file, const char *path, uint32_t flags);
int32_t vfs_create_path(vfs_file_t **out_file, const char *path, uint32_t flags);
void vfs_close_file(vfs_file_t *file);
int32_t vfs_read_file(vfs_file_t *file, void *buffer, uint32_t length);
int32_t vfs_write_file(vfs_file_t *file, const char *buffer, uint32_t length);
int32_t vfs_truncate_file(vfs_file_t *file, uint32_t length);
void vfs_rewind_file(vfs_file_t *file);
int32_t vfs_attach_stdio(vfs_file_t **fd_table, uint32_t fd_count);
int32_t vfs_clone_fds(vfs_file_t **destination, vfs_file_t **source, uint32_t fd_count);
void vfs_detach_fds(vfs_file_t **fd_table, uint32_t fd_count);
int32_t vfs_write_fd(vfs_file_t **fd_table, uint32_t fd_count, uint32_t fd, const char *buffer, uint32_t length);
uint32_t vfs_root_writable(void);
int32_t vfs_storage_selftest(void);

#endif
