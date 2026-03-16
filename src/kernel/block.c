#include "kernel/block.h"

#include <stdint.h>

#include "kernel/console.h"

#define BLOCK_MAX_DEVICES 8u

static block_device_t *devices[BLOCK_MAX_DEVICES];

static uint32_t string_length(const char *text) {
    uint32_t length = 0u;

    if (text == 0) {
        return 0u;
    }

    while (text[length] != '\0') {
        length++;
    }

    return length;
}

static int string_equals(const char *left, const char *right) {
    uint32_t index;
    uint32_t left_length = string_length(left);
    uint32_t right_length = string_length(right);

    if (left_length != right_length) {
        return 0;
    }

    for (index = 0u; index < left_length; index++) {
        if (left[index] != right[index]) {
            return 0;
        }
    }

    return 1;
}

void block_init(void) {
    uint32_t index;

    for (index = 0u; index < BLOCK_MAX_DEVICES; index++) {
        devices[index] = 0;
    }
}

int32_t block_register(block_device_t *device) {
    uint32_t index;

    if (device == 0 || device->name == 0 || device->block_size == 0u || device->block_count == 0u || device->read == 0) {
        return -1;
    }

    if (device->read_only == 0u && device->write == 0) {
        return -1;
    }

    for (index = 0u; index < BLOCK_MAX_DEVICES; index++) {
        if (devices[index] == 0) {
            devices[index] = device;
            console_write("block: registered ");
            console_write(device->name);
            console_write(" blocks 0x");
            console_write_hex32(device->block_count);
            console_write("\n");
            return 0;
        }

        if (string_equals(devices[index]->name, device->name)) {
            return -1;
        }
    }

    return -1;
}

block_device_t *block_find(const char *name) {
    uint32_t index;

    for (index = 0u; index < BLOCK_MAX_DEVICES; index++) {
        if (devices[index] == 0) {
            continue;
        }

        if (string_equals(devices[index]->name, name)) {
            return devices[index];
        }
    }

    return 0;
}

int32_t block_read(block_device_t *device, uint32_t block_index, uint32_t block_count, void *buffer) {
    if (device == 0 || buffer == 0 || block_count == 0u) {
        return -1;
    }

    if (block_index >= device->block_count || block_count > (device->block_count - block_index)) {
        return -1;
    }

    return device->read(device, block_index, block_count, buffer);
}

int32_t block_write(block_device_t *device, uint32_t block_index, uint32_t block_count, const void *buffer) {
    if (device == 0 || buffer == 0 || block_count == 0u || device->read_only != 0u || device->write == 0) {
        return -1;
    }

    if (block_index >= device->block_count || block_count > (device->block_count - block_index)) {
        return -1;
    }

    return device->write(device, block_index, block_count, buffer);
}
