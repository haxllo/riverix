#ifndef RIVERIX_PARTITION_H
#define RIVERIX_PARTITION_H

#include <stdint.h>

#include "kernel/block.h"

int32_t partition_register_rootfs(block_device_t *disk, const char *device_name);

#endif
