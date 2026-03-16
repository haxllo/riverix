#ifndef RIVERIX_BLOCK_H
#define RIVERIX_BLOCK_H

#include <stdint.h>

typedef struct block_device block_device_t;

typedef int32_t (*block_read_op_t)(block_device_t *device, uint32_t block_index, uint32_t block_count, void *buffer);
typedef int32_t (*block_write_op_t)(block_device_t *device, uint32_t block_index, uint32_t block_count, const void *buffer);

struct block_device {
    const char *name;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t read_only;
    block_read_op_t read;
    block_write_op_t write;
    void *context;
    block_device_t *parent;
};

void block_init(void);
int32_t block_register(block_device_t *device);
block_device_t *block_find(const char *name);
int32_t block_read(block_device_t *device, uint32_t block_index, uint32_t block_count, void *buffer);
int32_t block_write(block_device_t *device, uint32_t block_index, uint32_t block_count, const void *buffer);

#endif
