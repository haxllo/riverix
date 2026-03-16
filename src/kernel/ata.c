#include "kernel/ata.h"

#include <stdint.h>

#include "kernel/block.h"
#include "kernel/console.h"
#include "kernel/io.h"

#define ATA_PRIMARY_IO 0x1F0u
#define ATA_PRIMARY_CONTROL 0x3F6u

#define ATA_REG_DATA 0x00u
#define ATA_REG_SECTOR_COUNT 0x02u
#define ATA_REG_LBA_LOW 0x03u
#define ATA_REG_LBA_MID 0x04u
#define ATA_REG_LBA_HIGH 0x05u
#define ATA_REG_DRIVE_HEAD 0x06u
#define ATA_REG_STATUS 0x07u
#define ATA_REG_COMMAND 0x07u

#define ATA_STATUS_ERR 0x01u
#define ATA_STATUS_DRQ 0x08u
#define ATA_STATUS_DF 0x20u
#define ATA_STATUS_DRDY 0x40u
#define ATA_STATUS_BSY 0x80u

#define ATA_CMD_READ_SECTORS 0x20u
#define ATA_CMD_WRITE_SECTORS 0x30u
#define ATA_CMD_IDENTIFY 0xECu

#define ATA_DRIVE_MASTER 0xE0u
#define ATA_IDENTIFY_WORDS 256u
#define ATA_POLL_LIMIT 1000000u

typedef struct ata_device_context {
    uint16_t io_base;
    uint16_t control_base;
} ata_device_context_t;

static ata_device_context_t ata0_context;
static block_device_t ata0_device;
static uint32_t ata_initialized;

static uint8_t ata_status(ata_device_context_t *context) {
    return inb((uint16_t)(context->io_base + ATA_REG_STATUS));
}

static void ata_delay_400ns(ata_device_context_t *context) {
    (void)inb(context->control_base);
    (void)inb(context->control_base);
    (void)inb(context->control_base);
    (void)inb(context->control_base);
}

static int ata_wait_not_busy(ata_device_context_t *context) {
    uint32_t attempts;

    for (attempts = 0u; attempts < ATA_POLL_LIMIT; attempts++) {
        uint8_t status = ata_status(context);

        if (status == 0u || status == 0xFFu) {
            return -1;
        }

        if ((status & ATA_STATUS_BSY) == 0u) {
            return 0;
        }
    }

    return -1;
}

static int ata_wait_ready(ata_device_context_t *context) {
    uint32_t attempts;

    for (attempts = 0u; attempts < ATA_POLL_LIMIT; attempts++) {
        uint8_t status = ata_status(context);

        if ((status & ATA_STATUS_BSY) != 0u) {
            continue;
        }

        if ((status & (ATA_STATUS_ERR | ATA_STATUS_DF)) != 0u) {
            return -1;
        }

        if ((status & ATA_STATUS_DRQ) != 0u) {
            return 0;
        }

        if ((status & ATA_STATUS_DRDY) == 0u) {
            io_wait();
        }
    }

    return -1;
}

static int ata_identify(ata_device_context_t *context, uint16_t *words) {
    uint8_t status;
    uint32_t index;

    if (ata_wait_not_busy(context) != 0) {
        return -1;
    }

    outb((uint16_t)(context->io_base + ATA_REG_DRIVE_HEAD), ATA_DRIVE_MASTER);
    ata_delay_400ns(context);

    outb((uint16_t)(context->io_base + ATA_REG_SECTOR_COUNT), 0u);
    outb((uint16_t)(context->io_base + ATA_REG_LBA_LOW), 0u);
    outb((uint16_t)(context->io_base + ATA_REG_LBA_MID), 0u);
    outb((uint16_t)(context->io_base + ATA_REG_LBA_HIGH), 0u);
    outb((uint16_t)(context->io_base + ATA_REG_COMMAND), ATA_CMD_IDENTIFY);

    status = ata_status(context);
    if (status == 0u || status == 0xFFu) {
        return -1;
    }

    if (inb((uint16_t)(context->io_base + ATA_REG_LBA_MID)) != 0u || inb((uint16_t)(context->io_base + ATA_REG_LBA_HIGH)) != 0u) {
        return -1;
    }

    if (ata_wait_ready(context) != 0) {
        return -1;
    }

    for (index = 0u; index < ATA_IDENTIFY_WORDS; index++) {
        words[index] = inw((uint16_t)(context->io_base + ATA_REG_DATA));
    }

    ata_delay_400ns(context);
    return 0;
}

static int ata_transfer(block_device_t *device, uint32_t block_index, uint32_t block_count, void *buffer, uint8_t command) {
    ata_device_context_t *context = (ata_device_context_t *)device->context;
    uint8_t *bytes = (uint8_t *)buffer;
    uint32_t blocks_remaining = block_count;
    uint32_t current_lba = block_index;

    if (context == 0 || buffer == 0) {
        return -1;
    }

    while (blocks_remaining != 0u) {
        uint8_t chunk_count = blocks_remaining > 255u ? 255u : (uint8_t)blocks_remaining;
        uint32_t sector;

        if (ata_wait_not_busy(context) != 0) {
            return -1;
        }

        outb((uint16_t)(context->io_base + ATA_REG_DRIVE_HEAD), (uint8_t)(ATA_DRIVE_MASTER | ((current_lba >> 24) & 0x0Fu)));
        ata_delay_400ns(context);

        outb((uint16_t)(context->io_base + ATA_REG_SECTOR_COUNT), chunk_count);
        outb((uint16_t)(context->io_base + ATA_REG_LBA_LOW), (uint8_t)(current_lba & 0xFFu));
        outb((uint16_t)(context->io_base + ATA_REG_LBA_MID), (uint8_t)((current_lba >> 8) & 0xFFu));
        outb((uint16_t)(context->io_base + ATA_REG_LBA_HIGH), (uint8_t)((current_lba >> 16) & 0xFFu));
        outb((uint16_t)(context->io_base + ATA_REG_COMMAND), command);

        for (sector = 0u; sector < chunk_count; sector++) {
            uint16_t *sector_words = (uint16_t *)&bytes[sector * 512u];
            uint32_t word_index;

            if (ata_wait_ready(context) != 0) {
                return -1;
            }

            if (command == ATA_CMD_READ_SECTORS) {
                for (word_index = 0u; word_index < 256u; word_index++) {
                    sector_words[word_index] = inw((uint16_t)(context->io_base + ATA_REG_DATA));
                }
            } else {
                for (word_index = 0u; word_index < 256u; word_index++) {
                    __asm__ volatile ("outw %0, %1" : : "a"(sector_words[word_index]), "Nd"((uint16_t)(context->io_base + ATA_REG_DATA)));
                }
            }

            ata_delay_400ns(context);
        }

        bytes += ((uint32_t)chunk_count * 512u);
        current_lba += chunk_count;
        blocks_remaining -= chunk_count;
    }

    return 0;
}

static int32_t ata_read(block_device_t *device, uint32_t block_index, uint32_t block_count, void *buffer) {
    return ata_transfer(device, block_index, block_count, buffer, ATA_CMD_READ_SECTORS);
}

static int32_t ata_write(block_device_t *device, uint32_t block_index, uint32_t block_count, const void *buffer) {
    return ata_transfer(device, block_index, block_count, (void *)buffer, ATA_CMD_WRITE_SECTORS);
}

int32_t ata_init(void) {
    uint16_t identify_words[ATA_IDENTIFY_WORDS];
    uint32_t sector_count;

    if (ata_initialized != 0u) {
        return ata0_device.block_count != 0u ? 0 : -1;
    }

    ata_initialized = 1u;
    ata0_context.io_base = ATA_PRIMARY_IO;
    ata0_context.control_base = ATA_PRIMARY_CONTROL;

    if (ata_identify(&ata0_context, identify_words) != 0) {
        console_write("ata: primary master unavailable\n");
        return -1;
    }

    sector_count = ((uint32_t)identify_words[61] << 16) | identify_words[60];
    if (sector_count == 0u) {
        console_write("ata: identify reported zero sectors\n");
        return -1;
    }

    ata0_device.name = "ata0";
    ata0_device.block_size = 512u;
    ata0_device.block_count = sector_count;
    ata0_device.read_only = 0u;
    ata0_device.read = ata_read;
    ata0_device.write = ata_write;
    ata0_device.context = &ata0_context;
    ata0_device.parent = 0;

    if (block_register(&ata0_device) != 0) {
        console_write("ata: failed to register ata0\n");
        return -1;
    }

    console_write("ata: detected ata0 sectors 0x");
    console_write_hex32(sector_count);
    console_write("\n");
    return 0;
}
