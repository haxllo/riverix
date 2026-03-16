#ifndef RIVERIX_SIMPLEFS_H
#define RIVERIX_SIMPLEFS_H

#include <stdint.h>

#include "kernel/block.h"
#include "kernel/vfs.h"

int32_t simplefs_mount(block_device_t *device, vfs_inode_t **out_root);

#endif
