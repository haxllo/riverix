#ifndef RIVERIX_STORAGE_H
#define RIVERIX_STORAGE_H

#include <stdint.h>

#include "kernel/block.h"

int32_t storage_init(void);
block_device_t *storage_boot_disk(void);
const char *storage_boot_partition_name(void);

#endif
