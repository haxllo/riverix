#include "kernel/vfs.h"

#include <stdint.h>

#include "kernel/block.h"
#include "kernel/bootinfo.h"
#include "kernel/console.h"
#include "kernel/kheap.h"
#include "kernel/partition.h"
#include "kernel/ramdisk.h"
#include "kernel/serial.h"
#include "kernel/simplefs.h"
#include "kernel/storage.h"
#include "kernel/trace.h"

static int32_t console_inode_read(vfs_file_t *file, void *buffer, uint32_t length);
static int32_t console_inode_write(vfs_file_t *file, const char *buffer, uint32_t length);
static int32_t pipe_inode_read(vfs_file_t *file, void *buffer, uint32_t length);
static int32_t pipe_inode_write(vfs_file_t *file, const char *buffer, uint32_t length);
static const vfs_inode_t *vfs_resolve_path(const char *path, const vfs_credentials_t *credentials);
static int32_t vfs_resolve_parent(const char *path, const vfs_credentials_t *credentials, vfs_inode_t **out_parent, char *leaf_name);
static int32_t vfs_append_child(vfs_inode_t *directory, vfs_inode_t *child);
static int32_t vfs_mount_root(vfs_inode_t **out_root, block_device_t *device);
static int32_t vfs_ensure_disk_rootfs_device(block_device_t **out_device);
static int32_t vfs_try_mount_disk_root(vfs_inode_t **out_root);
static int32_t vfs_try_mount_ramdisk_root(const multiboot_info_t *multiboot_info, vfs_inode_t **out_root);

static const vfs_file_ops_t console_ops = {
    .read = console_inode_read,
    .write = console_inode_write,
};

static const vfs_file_ops_t pipe_ops = {
    .read = pipe_inode_read,
    .write = pipe_inode_write,
};

typedef struct vfs_pipe {
    vfs_inode_t inode;
    uint32_t reader_count;
    uint32_t writer_count;
    uint32_t read_index;
    uint32_t write_index;
    uint32_t buffered;
    uint8_t buffer[VFS_PIPE_BUFFER_SIZE];
} vfs_pipe_t;

static vfs_inode_t *root_inode;
static uint32_t root_writable;
static block_device_t *root_device;
static vfs_inode_t console_inode = {
    .name = "console",
    .kind = VFS_INODE_DEV,
    .ops = &console_ops,
    .inode_ops = 0,
    .ref_count = 0u,
    .data = 0,
    .size = 0u,
    .children = 0,
    .child_count = 0u,
    .child_capacity = 0u,
    .mode = VFS_MODE_IRUSR | VFS_MODE_IWUSR | VFS_MODE_IRGRP | VFS_MODE_IWGRP | VFS_MODE_IROTH | VFS_MODE_IWOTH,
    .uid = 0u,
    .gid = 0u,
    .links = 1u,
    .private_data = 0,
};
static vfs_inode_t tty_inode = {
    .name = "tty",
    .kind = VFS_INODE_DEV,
    .ops = &console_ops,
    .inode_ops = 0,
    .ref_count = 0u,
    .data = 0,
    .size = 0u,
    .children = 0,
    .child_count = 0u,
    .child_capacity = 0u,
    .mode = VFS_MODE_IRUSR | VFS_MODE_IWUSR | VFS_MODE_IRGRP | VFS_MODE_IWGRP | VFS_MODE_IROTH | VFS_MODE_IWOTH,
    .uid = 0u,
    .gid = 0u,
    .links = 1u,
    .private_data = 0,
};

static vfs_file_t *file_table;

#define VFS_REINSTALL_CHUNK_BLOCKS 8u

static uint32_t string_length(const char *text) {
    uint32_t length = 0u;

    while (text[length] != '\0') {
        length++;
    }

    return length;
}

static void zero_bytes(void *buffer, uint32_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    uint32_t index;

    for (index = 0u; index < length; index++) {
        bytes[index] = 0u;
    }
}

static void string_copy(char *destination, const char *source, uint32_t max_length) {
    uint32_t index = 0u;

    if (destination == 0 || source == 0 || max_length == 0u) {
        return;
    }

    while ((index + 1u) < max_length && source[index] != '\0') {
        destination[index] = source[index];
        index++;
    }

    destination[index] = '\0';
}

static int string_equals_component(const char *candidate, const char *component, uint32_t component_length) {
    uint32_t candidate_length = string_length(candidate);
    uint32_t index;

    if (candidate_length != component_length) {
        return 0;
    }

    for (index = 0u; index < component_length; index++) {
        if (candidate[index] != component[index]) {
            return 0;
        }
    }

    return 1;
}

static uint32_t parse_hex32(const char *text, uint32_t length) {
    uint32_t value = 0u;
    uint32_t index;

    for (index = 0u; index < length; index++) {
        uint8_t ch = (uint8_t)text[index];
        value <<= 4u;

        if (ch >= '0' && ch <= '9') {
            value |= (uint32_t)(ch - '0');
        } else if (ch >= 'A' && ch <= 'F') {
            value |= (uint32_t)(ch - 'A' + 10u);
        } else if (ch >= 'a' && ch <= 'f') {
            value |= (uint32_t)(ch - 'a' + 10u);
        } else {
            return 0u;
        }
    }

    return value;
}

static void format_hex32(char *buffer, uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    uint32_t index;

    for (index = 0u; index < 8u; index++) {
        buffer[index] = digits[(value >> ((7u - index) * 4u)) & 0xFu];
    }
}

static int vfs_is_superuser(const vfs_credentials_t *credentials) {
    return credentials == 0 || credentials->uid == 0u;
}

static uint32_t vfs_allowed_access(const vfs_inode_t *inode, const vfs_credentials_t *credentials) {
    uint32_t allowed = 0u;
    uint32_t mode;

    if (inode == 0) {
        return 0u;
    }

    if (vfs_is_superuser(credentials)) {
        return VFS_ACCESS_READ | VFS_ACCESS_WRITE | VFS_ACCESS_EXEC;
    }

    mode = inode->mode;
    if (credentials->uid == inode->uid) {
        if ((mode & VFS_MODE_IRUSR) != 0u) {
            allowed |= VFS_ACCESS_READ;
        }
        if ((mode & VFS_MODE_IWUSR) != 0u) {
            allowed |= VFS_ACCESS_WRITE;
        }
        if ((mode & VFS_MODE_IXUSR) != 0u) {
            allowed |= VFS_ACCESS_EXEC;
        }
        return allowed;
    }

    if (credentials->gid == inode->gid) {
        if ((mode & VFS_MODE_IRGRP) != 0u) {
            allowed |= VFS_ACCESS_READ;
        }
        if ((mode & VFS_MODE_IWGRP) != 0u) {
            allowed |= VFS_ACCESS_WRITE;
        }
        if ((mode & VFS_MODE_IXGRP) != 0u) {
            allowed |= VFS_ACCESS_EXEC;
        }
        return allowed;
    }

    if ((mode & VFS_MODE_IROTH) != 0u) {
        allowed |= VFS_ACCESS_READ;
    }
    if ((mode & VFS_MODE_IWOTH) != 0u) {
        allowed |= VFS_ACCESS_WRITE;
    }
    if ((mode & VFS_MODE_IXOTH) != 0u) {
        allowed |= VFS_ACCESS_EXEC;
    }
    return allowed;
}

static int vfs_has_access(const vfs_inode_t *inode, const vfs_credentials_t *credentials, uint32_t access_mask) {
    return (vfs_allowed_access(inode, credentials) & access_mask) == access_mask;
}

static vfs_pipe_t *vfs_pipe_from_file(vfs_file_t *file) {
    if (file == 0 || file->inode == 0 || file->inode->kind != VFS_INODE_PIPE || file->inode->private_data == 0) {
        return 0;
    }

    return (vfs_pipe_t *)file->inode->private_data;
}

static int32_t console_inode_read(vfs_file_t *file, void *buffer, uint32_t length) {
    char *bytes = (char *)buffer;
    uint32_t count = 0u;

    (void)file;

    if (buffer == 0) {
        return -1;
    }

    if (length == 0u) {
        return 0;
    }

    while (count < length) {
        char ch;

        if (!serial_can_read()) {
            return count != 0u ? (int32_t)count : VFS_ERR_WOULD_BLOCK;
        }

        ch = serial_read_char();

        if (ch == '\r') {
            ch = '\n';
        }

        if (ch == '\b' || ch == 0x7Fu) {
            if (count == 0u) {
                continue;
            }

            count--;
            console_write_len("\b \b", 3u);
            continue;
        }

        bytes[count++] = ch;
        console_write_len(&ch, 1u);

        if (ch == '\n') {
            break;
        }
    }

    return (int32_t)count;
}

static int32_t console_inode_write(vfs_file_t *file, const char *buffer, uint32_t length) {
    (void)file;

    if (buffer == 0) {
        return -1;
    }

    console_write_len(buffer, length);
    return (int32_t)length;
}

static int32_t pipe_inode_read(vfs_file_t *file, void *buffer, uint32_t length) {
    vfs_pipe_t *pipe = vfs_pipe_from_file(file);
    uint8_t *bytes = (uint8_t *)buffer;
    uint32_t copied = 0u;

    if (pipe == 0 || buffer == 0) {
        return -1;
    }

    if ((file->flags & VFS_O_RDONLY) == 0u) {
        return -1;
    }

    while (copied < length && pipe->buffered != 0u) {
        bytes[copied++] = pipe->buffer[pipe->read_index];
        pipe->read_index = (pipe->read_index + 1u) % VFS_PIPE_BUFFER_SIZE;
        pipe->buffered--;
    }

    if (copied != 0u) {
        return (int32_t)copied;
    }

    if (pipe->writer_count == 0u) {
        return 0;
    }

    return VFS_ERR_WOULD_BLOCK;
}

static int32_t pipe_inode_write(vfs_file_t *file, const char *buffer, uint32_t length) {
    vfs_pipe_t *pipe = vfs_pipe_from_file(file);
    uint32_t written = 0u;

    if (pipe == 0 || buffer == 0) {
        return -1;
    }

    if ((file->flags & VFS_O_WRONLY) == 0u) {
        return -1;
    }

    if (pipe->reader_count == 0u) {
        return -1;
    }

    while (written < length && pipe->buffered < VFS_PIPE_BUFFER_SIZE) {
        pipe->buffer[pipe->write_index] = (uint8_t)buffer[written++];
        pipe->write_index = (pipe->write_index + 1u) % VFS_PIPE_BUFFER_SIZE;
        pipe->buffered++;
    }

    if (written != 0u) {
        return (int32_t)written;
    }

    return VFS_ERR_WOULD_BLOCK;
}

static void fd_table_clear(vfs_file_t **fd_table, uint32_t fd_count) {
    uint32_t index;

    for (index = 0u; index < fd_count; index++) {
        fd_table[index] = 0;
    }
}

static vfs_file_t *vfs_alloc_file(void) {
    uint32_t index;

    if (file_table == 0) {
        return 0;
    }

    for (index = 0u; index < VFS_MAX_FILES; index++) {
        if (file_table[index].ref_count != 0u) {
            continue;
        }

        file_table[index].inode = 0;
        file_table[index].flags = 0u;
        file_table[index].offset = 0u;
        file_table[index].ref_count = 1u;
        file_table[index].private_data = 0;
        return &file_table[index];
    }

    return 0;
}

static void vfs_release_file(vfs_file_t *file) {
    vfs_inode_t *inode;

    if (file == 0 || file->ref_count == 0u) {
        return;
    }

    file->ref_count--;
    if (file->ref_count != 0u) {
        return;
    }

    inode = file->inode;
    if (inode != 0 && inode->ref_count != 0u) {
        inode->ref_count--;
    }

    if (inode != 0 && inode->kind == VFS_INODE_PIPE && inode->private_data != 0) {
        vfs_pipe_t *pipe = (vfs_pipe_t *)inode->private_data;

        if ((file->flags & VFS_O_RDONLY) != 0u && pipe->reader_count != 0u) {
            pipe->reader_count--;
        }

        if ((file->flags & VFS_O_WRONLY) != 0u && pipe->writer_count != 0u) {
            pipe->writer_count--;
        }

        if (inode->ref_count == 0u) {
            kfree(pipe);
        }
    }

    file->inode = 0;
    file->flags = 0u;
    file->offset = 0u;
    file->private_data = 0;
}

static vfs_file_t *vfs_open_inode(vfs_inode_t *inode, uint32_t flags) {
    vfs_file_t *file = vfs_alloc_file();

    if (file == 0) {
        return 0;
    }

    file->inode = inode;
    file->flags = flags;
    file->offset = 0u;
    file->private_data = 0;
    inode->ref_count++;
    return file;
}

static void vfs_fill_stat(const vfs_inode_t *inode, vfs_stat_t *stat) {
    if (inode == 0 || stat == 0) {
        return;
    }

    stat->kind = inode->kind;
    stat->size = inode->size;
    stat->child_count = inode->child_count;
    stat->mode = inode->mode;
    stat->uid = inode->uid;
    stat->gid = inode->gid;
    stat->links = inode->links;
}

static const vfs_inode_t *vfs_resolve_path(const char *path, const vfs_credentials_t *credentials) {
    const vfs_inode_t *current = root_inode;
    const char *cursor = path;

    if (root_inode == 0 || path == 0 || path[0] != '/') {
        return 0;
    }

    while (*cursor == '/') {
        cursor++;
    }

    if (*cursor == '\0') {
        return current;
    }

    while (*cursor != '\0') {
        char component[32];
        uint32_t component_length = 0u;
        uint32_t index;
        const vfs_inode_t *next = 0;

        if (current->kind != VFS_INODE_DIR || !vfs_has_access(current, credentials, VFS_ACCESS_EXEC)) {
            return 0;
        }

        while (*cursor != '\0' && *cursor != '/') {
            if (component_length + 1u >= sizeof(component)) {
                return 0;
            }

            component[component_length++] = *cursor++;
        }
        component[component_length] = '\0';

        for (index = 0u; index < current->child_count; index++) {
            if (string_equals_component(current->children[index].name, component, component_length)) {
                next = current->children[index].inode;
                break;
            }
        }

        if (next == 0) {
            return 0;
        }

        current = next;

        while (*cursor == '/') {
            cursor++;
        }
    }

    return current;
}

static int32_t vfs_resolve_parent(const char *path, const vfs_credentials_t *credentials, vfs_inode_t **out_parent, char *leaf_name) {
    char parent_path[VFS_PATH_MAX];
    uint32_t parent_length = 0u;
    uint32_t path_length = string_length(path);
    uint32_t last_slash = 0u;
    uint32_t index;

    if (path == 0 || out_parent == 0 || leaf_name == 0 || path[0] != '/' || path_length < 2u) {
        return -1;
    }

    for (index = 0u; index < path_length; index++) {
        if (path[index] == '/') {
            last_slash = index;
        }
    }

    if (last_slash == (path_length - 1u)) {
        return -1;
    }

    for (index = last_slash + 1u; index < path_length; index++) {
        if ((index - last_slash) >= VFS_NAME_MAX) {
            return -1;
        }

        leaf_name[index - last_slash - 1u] = path[index];
    }
    leaf_name[path_length - last_slash - 1u] = '\0';

    if (last_slash == 0u) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        parent_length = last_slash;
        if (parent_length >= sizeof(parent_path)) {
            return -1;
        }

        for (index = 0u; index < parent_length; index++) {
            parent_path[index] = path[index];
        }
        parent_path[parent_length] = '\0';
    }

    *out_parent = (vfs_inode_t *)(uintptr_t)vfs_resolve_path(parent_path, credentials);
    return *out_parent != 0 && (*out_parent)->kind == VFS_INODE_DIR ? 0 : -1;
}

static int32_t vfs_append_child(vfs_inode_t *directory, vfs_inode_t *child) {
    if (directory == 0 || child == 0 || directory->kind != VFS_INODE_DIR || directory->children == 0) {
        return -1;
    }

    if (directory->child_count >= directory->child_capacity) {
        return -1;
    }

    directory->children[directory->child_count].name = child->name;
    directory->children[directory->child_count].inode = child;
    directory->child_count++;
    return 0;
}

static int32_t vfs_mount_root(vfs_inode_t **out_root, block_device_t *device) {
    if (device == 0 || out_root == 0) {
        return -1;
    }

    return simplefs_mount(device, out_root);
}

static int32_t vfs_ensure_disk_rootfs_device(block_device_t **out_device) {
    block_device_t *rootfs_device;
    block_device_t *disk_device;
    const char *partition_name;

    if (out_device == 0) {
        return -1;
    }

    if (storage_init() != 0) {
        return -1;
    }

    partition_name = storage_boot_partition_name();
    if (partition_name == 0) {
        return -1;
    }

    rootfs_device = block_find(partition_name);
    if (rootfs_device != 0) {
        *out_device = rootfs_device;
        return 0;
    }

    disk_device = storage_boot_disk();
    if (disk_device == 0) {
        return -1;
    }

    if (partition_register_rootfs(disk_device, partition_name) != 0) {
        return -1;
    }

    rootfs_device = block_find(partition_name);
    if (rootfs_device == 0) {
        return -1;
    }

    *out_device = rootfs_device;
    return 0;
}

static int32_t vfs_try_mount_disk_root(vfs_inode_t **out_root) {
    block_device_t *rootfs_device;

    if (vfs_ensure_disk_rootfs_device(&rootfs_device) != 0) {
        return -1;
    }

    if (vfs_mount_root(out_root, rootfs_device) != 0) {
        console_write("vfs: disk rootfs mount failed\n");
        return -1;
    }

    root_writable = rootfs_device->read_only == 0u ? 1u : 0u;
    root_device = rootfs_device;
    console_write("vfs: rootfs mounted from disk\n");
    trace_log(SYS_TRACE_CATEGORY_BLOCK,
              SYS_TRACE_EVENT_BLOCK_ROOTFS,
              1u,
              root_writable,
              rootfs_device->block_count);
    return 0;
}

static int32_t vfs_try_mount_ramdisk_root(const multiboot_info_t *multiboot_info, vfs_inode_t **out_root) {
    block_device_t *rootfs_device;

    if (ramdisk_register_rootfs(multiboot_info, "rootfs0") != 0) {
        console_write("vfs: rootfs ramdisk unavailable\n");
        return -1;
    }

    rootfs_device = block_find("rootfs0");
    if (rootfs_device == 0) {
        console_write("vfs: rootfs block device missing\n");
        return -1;
    }

    if (vfs_mount_root(out_root, rootfs_device) != 0) {
        console_write("vfs: rootfs ramdisk mount failed\n");
        return -1;
    }

    root_writable = 0u;
    root_device = rootfs_device;
    console_write("vfs: rootfs mounted from ramdisk\n");
    trace_log(SYS_TRACE_CATEGORY_BLOCK,
              SYS_TRACE_EVENT_BLOCK_ROOTFS,
              2u,
              0u,
              rootfs_device->block_count);
    return 0;
}

void vfs_init(const multiboot_info_t *multiboot_info) {
    vfs_inode_t *dev_directory;
    boot_root_policy_t root_policy;

    root_inode = 0;
    root_writable = 0u;
    root_device = 0;

    if (file_table == 0) {
        file_table = (vfs_file_t *)kzalloc(sizeof(vfs_file_t) * VFS_MAX_FILES);
        if (file_table == 0) {
            console_write("vfs: no file table\n");
            return;
        }
    } else {
        zero_bytes(file_table, sizeof(vfs_file_t) * VFS_MAX_FILES);
    }

    block_init();
    root_policy = bootinfo_root_policy();
    console_write("vfs: root policy ");
    console_write(bootinfo_root_policy_name(root_policy));
    console_write("\n");

    switch (root_policy) {
    case BOOT_ROOT_DISK:
        if (vfs_try_mount_disk_root(&root_inode) != 0) {
            root_inode = 0;
            console_write("vfs: forced disk rootfs unavailable\n");
            return;
        }
        break;
    case BOOT_ROOT_RAMDISK:
        if (vfs_try_mount_ramdisk_root(multiboot_info, &root_inode) != 0) {
            root_inode = 0;
            console_write("vfs: forced ramdisk rootfs unavailable\n");
            return;
        }
        break;
    default:
        if (vfs_try_mount_disk_root(&root_inode) != 0 && vfs_try_mount_ramdisk_root(multiboot_info, &root_inode) != 0) {
            root_inode = 0;
            console_write("vfs: no bootable rootfs\n");
            return;
        }
        break;
    }

    console_inode.ref_count = 0u;
    tty_inode.ref_count = 0u;
    dev_directory = (vfs_inode_t *)(uintptr_t)vfs_resolve_path("/dev", 0);
    if (dev_directory == 0 ||
        vfs_append_child(dev_directory, &console_inode) != 0 ||
        vfs_append_child(dev_directory, &tty_inode) != 0) {
        root_inode = 0;
        console_write("vfs: console/tty attach failed\n");
        return;
    }

    console_write("vfs: ready\n");
}

int32_t vfs_open_path(vfs_file_t **out_file, const char *path, uint32_t flags, const vfs_credentials_t *credentials) {
    vfs_inode_t *inode = (vfs_inode_t *)(uintptr_t)vfs_resolve_path(path, credentials);
    uint32_t required_access = 0u;

    if (out_file == 0 || inode == 0 || inode->kind == VFS_INODE_DIR) {
        return -1;
    }

    if ((flags & VFS_O_RDONLY) != 0u) {
        required_access |= VFS_ACCESS_READ;
    }
    if ((flags & VFS_O_WRONLY) != 0u) {
        required_access |= VFS_ACCESS_WRITE;
    }

    if (required_access != 0u && !vfs_has_access(inode, credentials, required_access)) {
        return -1;
    }

    if ((flags & VFS_O_WRONLY) != 0u && (inode->ops == 0 || inode->ops->write == 0)) {
        return -1;
    }

    *out_file = vfs_open_inode(inode, flags);
    return *out_file != 0 ? 0 : -1;
}

int32_t vfs_create_path(vfs_file_t **out_file, const char *path, uint32_t flags, uint32_t mode, const vfs_credentials_t *credentials) {
    vfs_inode_t *parent;
    vfs_inode_t *inode;
    char leaf_name[32];

    if (out_file == 0 || path == 0 || vfs_resolve_path(path, credentials) != 0) {
        return -1;
    }

    if (vfs_resolve_parent(path, credentials, &parent, leaf_name) != 0 ||
        !vfs_has_access(parent, credentials, VFS_ACCESS_WRITE | VFS_ACCESS_EXEC) ||
        parent->inode_ops == 0 ||
        parent->inode_ops->create_child == 0) {
        return -1;
    }

    if (parent->inode_ops->create_child(parent, leaf_name, VFS_INODE_REG, mode, credentials, &inode) != 0) {
        return -1;
    }

    *out_file = vfs_open_inode(inode, flags);
    return *out_file != 0 ? 0 : -1;
}

void vfs_close_file(vfs_file_t *file) {
    vfs_release_file(file);
}

int32_t vfs_read_file(vfs_file_t *file, void *buffer, uint32_t length) {
    if (file == 0 || file->inode == 0 || file->inode->ops == 0 || file->inode->ops->read == 0) {
        return -1;
    }

    if ((file->flags & VFS_O_RDONLY) == 0u) {
        return -1;
    }

    return file->inode->ops->read(file, buffer, length);
}

int32_t vfs_write_file(vfs_file_t *file, const char *buffer, uint32_t length) {
    if (file == 0 || file->inode == 0 || file->inode->ops == 0 || file->inode->ops->write == 0 || buffer == 0) {
        return -1;
    }

    if ((file->flags & VFS_O_WRONLY) == 0u) {
        return -1;
    }

    return file->inode->ops->write(file, buffer, length);
}

int32_t vfs_seek_file(vfs_file_t *file, int32_t offset, uint32_t whence) {
    int64_t base;
    int64_t target;

    if (file == 0 || file->inode == 0 || file->inode->kind == VFS_INODE_DIR) {
        return -1;
    }

    switch (whence) {
    case VFS_SEEK_SET:
        base = 0;
        break;
    case VFS_SEEK_CUR:
        base = (int64_t)file->offset;
        break;
    case VFS_SEEK_END:
        base = (int64_t)file->inode->size;
        break;
    default:
        return -1;
    }

    target = base + (int64_t)offset;
    if (target < 0 || target > 0x7FFFFFFFll) {
        return -1;
    }

    file->offset = (uint32_t)target;
    return (int32_t)file->offset;
}

int32_t vfs_truncate_file(vfs_file_t *file, uint32_t length) {
    if (file == 0 || file->inode == 0 || file->inode->inode_ops == 0 || file->inode->inode_ops->resize == 0) {
        return -1;
    }

    if (file->inode->inode_ops->resize(file->inode, length) != 0) {
        return -1;
    }

    if (file->offset > length) {
        file->offset = length;
    }

    return 0;
}

void vfs_rewind_file(vfs_file_t *file) {
    if (file != 0) {
        file->offset = 0u;
    }
}

void vfs_retain_file(vfs_file_t *file) {
    if (file != 0 && file->ref_count != 0u) {
        file->ref_count++;
    }
}

int32_t vfs_mkdir_path(const char *path, uint32_t mode, const vfs_credentials_t *credentials) {
    vfs_inode_t *parent;
    vfs_inode_t *inode;
    char leaf_name[VFS_NAME_MAX];

    if (path == 0 || vfs_resolve_path(path, credentials) != 0) {
        return -1;
    }

    if (vfs_resolve_parent(path, credentials, &parent, leaf_name) != 0 ||
        !vfs_has_access(parent, credentials, VFS_ACCESS_WRITE | VFS_ACCESS_EXEC) ||
        parent->inode_ops == 0 ||
        parent->inode_ops->create_child == 0) {
        return -1;
    }

    return parent->inode_ops->create_child(parent, leaf_name, VFS_INODE_DIR, mode, credentials, &inode);
}

int32_t vfs_unlink_path(const char *path, const vfs_credentials_t *credentials) {
    vfs_inode_t *parent;
    const vfs_inode_t *child;
    char leaf_name[VFS_NAME_MAX];

    if (path == 0 || path[0] != '/' || path[1] == '\0') {
        return -1;
    }

    if (vfs_resolve_parent(path, credentials, &parent, leaf_name) != 0 ||
        !vfs_has_access(parent, credentials, VFS_ACCESS_WRITE | VFS_ACCESS_EXEC) ||
        parent->inode_ops == 0 ||
        parent->inode_ops->remove_child == 0) {
        return -1;
    }

    child = vfs_resolve_path(path, credentials);
    if (child == 0) {
        return -1;
    }

    if ((parent->mode & VFS_MODE_ISVTX) != 0u &&
        !vfs_is_superuser(credentials) &&
        credentials->uid != parent->uid &&
        credentials->uid != child->uid) {
        return -1;
    }

    return parent->inode_ops->remove_child(parent, leaf_name);
}

int32_t vfs_access_path(const char *path, const vfs_credentials_t *credentials, uint32_t access_mask) {
    const vfs_inode_t *inode = vfs_resolve_path(path, credentials);

    if (inode == 0) {
        return -1;
    }

    return vfs_has_access(inode, credentials, access_mask) ? 0 : -1;
}

int32_t vfs_stat_path(const char *path, const vfs_credentials_t *credentials, vfs_stat_t *stat) {
    const vfs_inode_t *inode = vfs_resolve_path(path, credentials);

    if (path == 0 || stat == 0 || inode == 0) {
        return -1;
    }

    vfs_fill_stat(inode, stat);
    return 0;
}

int32_t vfs_readdir_path(const char *path, const vfs_credentials_t *credentials, uint32_t index, vfs_dirent_info_t *entry) {
    const vfs_inode_t *directory = vfs_resolve_path(path, credentials);
    const vfs_dir_entry_t *child;

    if (path == 0 ||
        entry == 0 ||
        directory == 0 ||
        directory->kind != VFS_INODE_DIR ||
        !vfs_has_access(directory, credentials, VFS_ACCESS_READ)) {
        return -1;
    }

    if (index >= directory->child_count) {
        return 1;
    }

    child = &directory->children[index];
    entry->kind = child->inode != 0 ? child->inode->kind : VFS_INODE_NONE;
    entry->size = child->inode != 0 ? child->inode->size : 0u;
    entry->mode = child->inode != 0 ? child->inode->mode : 0u;
    entry->uid = child->inode != 0 ? child->inode->uid : 0u;
    entry->gid = child->inode != 0 ? child->inode->gid : 0u;
    entry->links = child->inode != 0 ? child->inode->links : 0u;
    zero_bytes(entry->name, sizeof(entry->name));
    string_copy(entry->name, child->name, sizeof(entry->name));
    return 0;
}

int32_t vfs_attach_stdio(vfs_file_t **fd_table, uint32_t fd_count, const vfs_credentials_t *credentials) {
    vfs_file_t *stdin_file;
    vfs_file_t *stdout_file;
    vfs_file_t *stderr_file;

    if (fd_table == 0 || fd_count < 3u) {
        return -1;
    }

    fd_table_clear(fd_table, fd_count);

    if (vfs_open_path(&stdin_file, "/dev/console", VFS_O_RDONLY, credentials) != 0) {
        return -1;
    }

    if (vfs_open_path(&stdout_file, "/dev/console", VFS_O_WRONLY, credentials) != 0) {
        vfs_close_file(stdin_file);
        return -1;
    }

    if (vfs_open_path(&stderr_file, "/dev/console", VFS_O_WRONLY, credentials) != 0) {
        vfs_close_file(stdin_file);
        vfs_close_file(stdout_file);
        return -1;
    }

    fd_table[0] = stdin_file;
    fd_table[1] = stdout_file;
    fd_table[2] = stderr_file;
    return 0;
}

int32_t vfs_clone_fds(vfs_file_t **destination, vfs_file_t **source, uint32_t fd_count) {
    uint32_t index;

    if (destination == 0 || source == 0) {
        return -1;
    }

    fd_table_clear(destination, fd_count);

    for (index = 0u; index < fd_count; index++) {
        destination[index] = source[index];
        if (destination[index] != 0) {
            destination[index]->ref_count++;
        }
    }

    return 0;
}

void vfs_detach_fds(vfs_file_t **fd_table, uint32_t fd_count) {
    uint32_t index;

    if (fd_table == 0) {
        return;
    }

    for (index = 0u; index < fd_count; index++) {
        if (fd_table[index] == 0) {
            continue;
        }

        vfs_release_file(fd_table[index]);
        fd_table[index] = 0;
    }
}

int32_t vfs_read_fd(vfs_file_t **fd_table, uint32_t fd_count, uint32_t fd, void *buffer, uint32_t length) {
    vfs_file_t *file;

    if (fd_table == 0 || buffer == 0 || fd >= fd_count) {
        return -1;
    }

    file = fd_table[fd];
    if (file == 0) {
        return -1;
    }

    return vfs_read_file(file, buffer, length);
}

int32_t vfs_write_fd(vfs_file_t **fd_table, uint32_t fd_count, uint32_t fd, const char *buffer, uint32_t length) {
    vfs_file_t *file;

    if (fd_table == 0 || buffer == 0 || fd >= fd_count) {
        return -1;
    }

    file = fd_table[fd];
    if (file == 0 || file->inode == 0 || file->inode->ops == 0 || file->inode->ops->write == 0) {
        return -1;
    }

    if ((file->flags & VFS_O_WRONLY) == 0u) {
        return -1;
    }

    return file->inode->ops->write(file, buffer, length);
}

int32_t vfs_create_pipe(vfs_file_t **out_read_file, vfs_file_t **out_write_file) {
    vfs_pipe_t *pipe;
    vfs_file_t *read_file;
    vfs_file_t *write_file;

    if (out_read_file == 0 || out_write_file == 0) {
        return -1;
    }

    pipe = (vfs_pipe_t *)kzalloc(sizeof(*pipe));
    if (pipe == 0) {
        return -1;
    }

    pipe->inode.name = "pipe";
    pipe->inode.kind = VFS_INODE_PIPE;
    pipe->inode.ops = &pipe_ops;
    pipe->inode.inode_ops = 0;
    pipe->inode.ref_count = 0u;
    pipe->inode.data = 0;
    pipe->inode.size = 0u;
    pipe->inode.children = 0;
    pipe->inode.child_count = 0u;
    pipe->inode.child_capacity = 0u;
    pipe->inode.mode = VFS_MODE_IRUSR | VFS_MODE_IWUSR;
    pipe->inode.uid = 0u;
    pipe->inode.gid = 0u;
    pipe->inode.links = 0u;
    pipe->inode.private_data = pipe;

    read_file = vfs_open_inode(&pipe->inode, VFS_O_RDONLY);
    if (read_file == 0) {
        kfree(pipe);
        return -1;
    }

    write_file = vfs_open_inode(&pipe->inode, VFS_O_WRONLY);
    if (write_file == 0) {
        vfs_close_file(read_file);
        return -1;
    }

    pipe->reader_count = 1u;
    pipe->writer_count = 1u;
    *out_read_file = read_file;
    *out_write_file = write_file;
    return 0;
}

int vfs_file_is_pipe(const vfs_file_t *file) {
    return file != 0 && file->inode != 0 && file->inode->kind == VFS_INODE_PIPE;
}

int32_t vfs_pipe_read(vfs_file_t *file, void *buffer, uint32_t length) {
    return pipe_inode_read(file, buffer, length);
}

int32_t vfs_pipe_write(vfs_file_t *file, const char *buffer, uint32_t length) {
    return pipe_inode_write(file, buffer, length);
}

uint32_t vfs_root_writable(void) {
    return root_writable;
}

int32_t vfs_reinstall_rootfs(void) {
    block_device_t *source_device;
    block_device_t *destination_device;
    uint8_t *buffer;
    uint32_t chunk_blocks;
    uint32_t block_index;

    if (!bootinfo_recovery_enabled()) {
        console_write("recovery: reinstall denied\n");
        return -1;
    }

    source_device = block_find("rootfs0");
    if (source_device == 0 || root_device != source_device) {
        console_write("recovery: source rootfs unavailable\n");
        return -1;
    }

    if (vfs_ensure_disk_rootfs_device(&destination_device) != 0) {
        console_write("recovery: disk rootfs unavailable\n");
        return -1;
    }

    if (destination_device->read_only != 0u ||
        source_device->block_size != destination_device->block_size ||
        source_device->block_count != destination_device->block_count) {
        console_write("recovery: reinstall geometry mismatch\n");
        return -1;
    }

    chunk_blocks = VFS_REINSTALL_CHUNK_BLOCKS;
    if (chunk_blocks == 0u) {
        return -1;
    }

    buffer = (uint8_t *)kmalloc(source_device->block_size * chunk_blocks);
    if (buffer == 0) {
        console_write("recovery: reinstall no buffer\n");
        return -1;
    }

    console_write("recovery: reinstall begin blocks 0x");
    console_write_hex32(source_device->block_count);
    console_write("\n");

    for (block_index = 0u; block_index < source_device->block_count; block_index += chunk_blocks) {
        uint32_t remaining_blocks = source_device->block_count - block_index;
        uint32_t current_chunk = remaining_blocks < chunk_blocks ? remaining_blocks : chunk_blocks;

        if (block_read(source_device, block_index, current_chunk, buffer) != 0 ||
            block_write(destination_device, block_index, current_chunk, buffer) != 0) {
            kfree(buffer);
            console_write("recovery: reinstall failed at 0x");
            console_write_hex32(block_index);
            console_write("\n");
            return -1;
        }
    }

    kfree(buffer);
    console_write("recovery: reinstall ok\n");
    return 0;
}

int32_t vfs_storage_selftest(void) {
    vfs_file_t *file;
    char buffer[16];
    char encoded[9];
    int32_t read_result;
    uint32_t bootcount;

    if (root_writable == 0u) {
        console_write("storage: writable smoke skipped\n");
        return 0;
    }

    if (vfs_open_path(&file, "/var/bootcount", VFS_O_RDWR, 0) != 0) {
        if (vfs_create_path(&file, "/var/bootcount", VFS_O_RDWR, VFS_MODE_FILE_DEFAULT, 0) != 0) {
            console_write("storage: failed to create /var/bootcount\n");
            return -1;
        }
    }

    zero_bytes(buffer, sizeof(buffer));
    read_result = vfs_read_file(file, buffer, 8u);
    if (read_result < 0) {
        vfs_close_file(file);
        console_write("storage: failed to read /var/bootcount\n");
        return -1;
    }

    bootcount = read_result > 0 ? parse_hex32(buffer, (uint32_t)read_result) : 0u;
    bootcount++;
    format_hex32(encoded, bootcount);
    encoded[8] = '\0';

    if (vfs_truncate_file(file, 0u) != 0) {
        vfs_close_file(file);
        console_write("storage: failed to truncate /var/bootcount\n");
        return -1;
    }

    vfs_rewind_file(file);
    if (vfs_write_file(file, encoded, 8u) != 8) {
        vfs_close_file(file);
        console_write("storage: failed to write /var/bootcount\n");
        return -1;
    }

    vfs_close_file(file);

    console_write("storage: bootcount 0x");
    console_write_hex32(bootcount);
    console_write("\n");
    return 0;
}
