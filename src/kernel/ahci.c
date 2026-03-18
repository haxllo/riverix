#include "kernel/ahci.h"

#include <stdint.h>

#include "kernel/block.h"
#include "kernel/console.h"
#include "kernel/mmio.h"
#include "kernel/paging.h"
#include "kernel/palloc.h"
#include "kernel/pci.h"

#define AHCI_CLASS_CODE 0x01u
#define AHCI_SUBCLASS 0x06u
#define AHCI_PROG_IF 0x01u
#define AHCI_ABAR_BAR_INDEX 5u
#define AHCI_GHC_AE (1u << 31)
#define AHCI_PORT_CMD_ST (1u << 0)
#define AHCI_PORT_CMD_SUD (1u << 1)
#define AHCI_PORT_CMD_FRE (1u << 4)
#define AHCI_PORT_CMD_FR (1u << 14)
#define AHCI_PORT_CMD_CR (1u << 15)
#define AHCI_PORT_IS_TFES (1u << 30)
#define AHCI_PORT_DET_PRESENT 0x3u
#define AHCI_PORT_IPM_ACTIVE 0x1u
#define AHCI_SIG_SATA 0x00000101u
#define AHCI_TFD_BSY 0x80u
#define AHCI_TFD_DRQ 0x08u
#define AHCI_FIS_TYPE_REG_H2D 0x27u
#define ATA_CMD_IDENTIFY_DEVICE 0xECu
#define ATA_CMD_READ_DMA_EXT 0x25u
#define ATA_CMD_WRITE_DMA_EXT 0x35u
#define AHCI_POLL_LIMIT 1000000u
#define AHCI_COMMAND_SLOT 0u
#define AHCI_MAX_PRDT_ENTRIES 128u
#define AHCI_MAX_TRANSFER_BYTES (AHCI_MAX_PRDT_ENTRIES * PAGE_SIZE)

typedef struct ahci_hba_port {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t reserved0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t reserved1[11];
    uint32_t vendor[4];
} ahci_hba_port_t;

typedef struct ahci_hba_memory {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t reserved[0xA0 - 0x2C];
    uint8_t vendor[0x100 - 0xA0];
    ahci_hba_port_t ports[32];
} ahci_hba_memory_t;

typedef struct ahci_command_header {
    uint16_t flags;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved[4];
} __attribute__((packed)) ahci_command_header_t;

typedef struct ahci_prdt_entry {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved;
    uint32_t dbc_i;
} __attribute__((packed)) ahci_prdt_entry_t;

typedef struct ahci_command_table {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    ahci_prdt_entry_t prdt[AHCI_MAX_PRDT_ENTRIES];
} __attribute__((packed)) ahci_command_table_t;

typedef struct ahci_context {
    pci_address_t pci;
    volatile ahci_hba_memory_t *hba;
    volatile ahci_hba_port_t *port;
    uint32_t port_index;
    uint32_t command_list_phys;
    uint32_t fis_phys;
    uint32_t command_table_phys;
} ahci_context_t;

static ahci_context_t ahci0_context;
static block_controller_t ahci0_controller;
static block_device_t ahci0_device;
static uint32_t ahci_initialized;

static void ahci_log_port_state(const char *label, volatile ahci_hba_port_t *port) {
    console_write("ahci: ");
    console_write(label);
    console_write(" cmd 0x");
    console_write_hex32(port->cmd);
    console_write(" tfd 0x");
    console_write_hex32(port->tfd);
    console_write(" is 0x");
    console_write_hex32(port->is);
    console_write(" ci 0x");
    console_write_hex32(port->ci);
    console_write(" sact 0x");
    console_write_hex32(port->sact);
    console_write(" serr 0x");
    console_write_hex32(port->serr);
    console_write("\n");
}

static int ahci_wait_command_complete(volatile ahci_hba_port_t *port) {
    uint32_t attempts;

    for (attempts = 0u; attempts < AHCI_POLL_LIMIT; attempts++) {
        if ((port->ci & (1u << AHCI_COMMAND_SLOT)) == 0u) {
            return (port->is & AHCI_PORT_IS_TFES) == 0u ? 0 : -1;
        }
    }

    return -1;
}

static void ahci_stop_port(volatile ahci_hba_port_t *port) {
    uint32_t attempts;

    port->cmd &= ~AHCI_PORT_CMD_ST;
    for (attempts = 0u; attempts < AHCI_POLL_LIMIT; attempts++) {
        if ((port->cmd & AHCI_PORT_CMD_CR) == 0u) {
            break;
        }
    }

    port->cmd &= ~AHCI_PORT_CMD_FRE;
    for (attempts = 0u; attempts < AHCI_POLL_LIMIT; attempts++) {
        if ((port->cmd & AHCI_PORT_CMD_FR) == 0u &&
            (port->tfd & (AHCI_TFD_BSY | AHCI_TFD_DRQ)) == 0u) {
            break;
        }
    }
}

static int ahci_start_port(volatile ahci_hba_port_t *port) {
    uint32_t attempts;

    for (attempts = 0u; attempts < AHCI_POLL_LIMIT; attempts++) {
        if ((port->cmd & (AHCI_PORT_CMD_CR | AHCI_PORT_CMD_FR)) == 0u &&
            (port->tfd & (AHCI_TFD_BSY | AHCI_TFD_DRQ)) == 0u) {
            port->cmd |= AHCI_PORT_CMD_FRE | AHCI_PORT_CMD_SUD;
            port->cmd |= AHCI_PORT_CMD_ST;
            return 0;
        }
    }

    return -1;
}

static int ahci_port_sata_ready(volatile ahci_hba_port_t *port) {
    uint32_t ssts = port->ssts;
    uint32_t det = ssts & 0xFu;
    uint32_t ipm = (ssts >> 8) & 0xFu;

    return det == AHCI_PORT_DET_PRESENT && ipm == AHCI_PORT_IPM_ACTIVE && port->sig == AHCI_SIG_SATA;
}

static int ahci_setup_port(ahci_context_t *context) {
    ahci_command_header_t *headers;

    context->command_list_phys = palloc_alloc_page();
    context->fis_phys = palloc_alloc_page();
    context->command_table_phys = palloc_alloc_page();
    if (context->command_list_phys == 0u || context->fis_phys == 0u || context->command_table_phys == 0u) {
        return -1;
    }

    ahci_stop_port(context->port);

    {
        uint8_t *command_list = (uint8_t *)paging_phys_to_virt(context->command_list_phys);
        uint8_t *fis = (uint8_t *)paging_phys_to_virt(context->fis_phys);
        uint8_t *table = (uint8_t *)paging_phys_to_virt(context->command_table_phys);
        uint32_t index;

        for (index = 0u; index < PAGE_SIZE; index++) {
            command_list[index] = 0u;
            fis[index] = 0u;
            table[index] = 0u;
        }
    }

    context->port->clb = context->command_list_phys;
    context->port->clbu = 0u;
    context->port->fb = context->fis_phys;
    context->port->fbu = 0u;
    context->port->ci = 0u;
    context->port->sact = 0u;
    context->port->ie = 0u;
    context->port->is = 0xFFFFFFFFu;
    context->port->serr = 0xFFFFFFFFu;

    headers = (ahci_command_header_t *)paging_phys_to_virt(context->command_list_phys);
    headers[AHCI_COMMAND_SLOT].ctba = context->command_table_phys;
    headers[AHCI_COMMAND_SLOT].ctbau = 0u;

    return ahci_start_port(context->port);
}

static int ahci_build_prdt(ahci_prdt_entry_t *entries, const void *buffer, uint32_t byte_count) {
    uintptr_t cursor = (uintptr_t)buffer;
    uint32_t remaining = byte_count;
    uint32_t entry_count = 0u;

    while (remaining != 0u) {
        uint32_t physical_address = paging_resolve_physical_in(paging_current_directory_phys(), cursor);
        uint32_t page_offset = (uint32_t)(cursor & (PAGE_SIZE - 1u));
        uint32_t chunk;

        if (physical_address == 0u || entry_count >= AHCI_MAX_PRDT_ENTRIES) {
            return -1;
        }

        chunk = PAGE_SIZE - page_offset;
        if (chunk > remaining) {
            chunk = remaining;
        }

        entries[entry_count].dba = physical_address + page_offset;
        entries[entry_count].dbau = 0u;
        entries[entry_count].reserved = 0u;
        entries[entry_count].dbc_i = (chunk - 1u);

        remaining -= chunk;
        cursor += chunk;
        entry_count++;
    }

    return (int)entry_count;
}

static void ahci_build_fis(uint8_t *cfis, uint8_t command, uint64_t lba, uint8_t device, uint16_t sector_count) {
    uint32_t index;

    for (index = 0u; index < 64u; index++) {
        cfis[index] = 0u;
    }

    cfis[0] = AHCI_FIS_TYPE_REG_H2D;
    cfis[1] = (1u << 7);
    cfis[2] = command;
    cfis[4] = (uint8_t)(lba & 0xFFu);
    cfis[5] = (uint8_t)((lba >> 8) & 0xFFu);
    cfis[6] = (uint8_t)((lba >> 16) & 0xFFu);
    cfis[7] = device;
    cfis[8] = (uint8_t)((lba >> 24) & 0xFFu);
    cfis[9] = (uint8_t)((lba >> 32) & 0xFFu);
    cfis[10] = (uint8_t)((lba >> 40) & 0xFFu);
    cfis[12] = (uint8_t)(sector_count & 0xFFu);
    cfis[13] = (uint8_t)((sector_count >> 8) & 0xFFu);
}

static int ahci_issue_transfer(ahci_context_t *context, uint8_t command, uint64_t lba, uint8_t device, uint16_t sector_count, void *buffer, uint32_t byte_count, int is_write) {
    ahci_command_header_t *headers = (ahci_command_header_t *)paging_phys_to_virt(context->command_list_phys);
    ahci_command_table_t *table = (ahci_command_table_t *)paging_phys_to_virt(context->command_table_phys);
    int prdt_count;
    uint32_t attempts;
    uint32_t index;

    for (attempts = 0u; attempts < AHCI_POLL_LIMIT; attempts++) {
        if ((context->port->tfd & (AHCI_TFD_BSY | AHCI_TFD_DRQ)) == 0u) {
            break;
        }
    }
    if (attempts == AHCI_POLL_LIMIT) {
        return -1;
    }

    for (index = 0u; index < sizeof(*table); index++) {
        ((uint8_t *)table)[index] = 0u;
    }

    for (index = 0u; index < sizeof(ahci_command_header_t); index++) {
        ((uint8_t *)&headers[AHCI_COMMAND_SLOT])[index] = 0u;
    }

    prdt_count = ahci_build_prdt(table->prdt, buffer, byte_count);
    if (prdt_count < 0) {
        return -1;
    }

    headers[AHCI_COMMAND_SLOT].flags = 5u | (is_write ? (1u << 6) : 0u);
    headers[AHCI_COMMAND_SLOT].prdtl = (uint16_t)prdt_count;
    headers[AHCI_COMMAND_SLOT].prdbc = 0u;
    headers[AHCI_COMMAND_SLOT].ctba = context->command_table_phys;
    headers[AHCI_COMMAND_SLOT].ctbau = 0u;

    ahci_build_fis(table->cfis, command, lba, device, sector_count);
    context->port->is = 0xFFFFFFFFu;
    context->port->ci = (1u << AHCI_COMMAND_SLOT);
    if (ahci_wait_command_complete(context->port) != 0) {
        ahci_log_port_state("command failed", context->port);
        console_write("ahci: command 0x");
        console_write_hex32(command);
        console_write(" prdbc 0x");
        console_write_hex32(headers[AHCI_COMMAND_SLOT].prdbc);
        console_write("\n");
        return -1;
    }

    return 0;
}

static int ahci_identify_device(ahci_context_t *context, uint16_t *identify_words) {
    return ahci_issue_transfer(context, ATA_CMD_IDENTIFY_DEVICE, 0u, 0xA0u, 0u, identify_words, 512u, 0);
}

static int32_t ahci_transfer(block_device_t *device, uint32_t block_index, uint32_t block_count, void *buffer, int is_write) {
    ahci_context_t *context = (ahci_context_t *)device->context;
    uint8_t *bytes = (uint8_t *)buffer;
    uint32_t remaining_blocks = block_count;
    uint64_t current_lba = block_index;

    if (context == 0 || buffer == 0) {
        return -1;
    }

    while (remaining_blocks != 0u) {
        uint32_t max_blocks = AHCI_MAX_TRANSFER_BYTES / device->block_size;
        uint16_t chunk_blocks = remaining_blocks > max_blocks ? (uint16_t)max_blocks : (uint16_t)remaining_blocks;
        uint32_t chunk_bytes = (uint32_t)chunk_blocks * device->block_size;
        uint8_t command = is_write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;

        if (ahci_issue_transfer(context, command, current_lba, (1u << 6), chunk_blocks, bytes, chunk_bytes, is_write) != 0) {
            return -1;
        }

        bytes += chunk_bytes;
        current_lba += chunk_blocks;
        remaining_blocks -= chunk_blocks;
    }

    return 0;
}

static int32_t ahci_read(block_device_t *device, uint32_t block_index, uint32_t block_count, void *buffer) {
    return ahci_transfer(device, block_index, block_count, buffer, 0);
}

static int32_t ahci_write(block_device_t *device, uint32_t block_index, uint32_t block_count, const void *buffer) {
    return ahci_transfer(device, block_index, block_count, (void *)buffer, 1);
}

int32_t ahci_init(void) {
    pci_address_t address;
    uint32_t abar;
    uint16_t identify_words[256];
    uint32_t sector_count_28 = 0u;
    uint64_t sector_count_48 = 0u;
    uint64_t sector_count = 0u;
    uint32_t pi;
    uint32_t port_index;

    if (ahci_initialized != 0u) {
        return ahci0_device.block_count != 0u ? 0 : -1;
    }

    ahci_initialized = 1u;

    if (pci_find_class(AHCI_CLASS_CODE, AHCI_SUBCLASS, AHCI_PROG_IF, &address) != 0) {
        console_write("ahci: controller unavailable\n");
        return -1;
    }

    pci_enable_memory_space(address);
    pci_enable_bus_master(address);

    abar = pci_read_bar(address, AHCI_ABAR_BAR_INDEX) & 0xFFFFFFF0u;
    if (abar == 0u) {
        console_write("ahci: invalid abar\n");
        return -1;
    }

    ahci0_context.pci = address;
    ahci0_context.hba = (volatile ahci_hba_memory_t *)(uintptr_t)mmio_map_region(abar, sizeof(ahci_hba_memory_t));
    if (ahci0_context.hba == 0) {
        console_write("ahci: abar map failed\n");
        return -1;
    }

    ahci0_context.hba->ghc |= AHCI_GHC_AE;
    pi = ahci0_context.hba->pi;

    for (port_index = 0u; port_index < 32u; port_index++) {
        if ((pi & (1u << port_index)) == 0u) {
            continue;
        }

        if (!ahci_port_sata_ready(&ahci0_context.hba->ports[port_index])) {
            continue;
        }

        ahci0_context.port = &ahci0_context.hba->ports[port_index];
        ahci0_context.port_index = port_index;
        break;
    }

    if (ahci0_context.port == 0) {
        console_write("ahci: no sata port ready\n");
        return -1;
    }

    ahci_log_port_state("port ready", ahci0_context.port);

    if (ahci_setup_port(&ahci0_context) != 0) {
        console_write("ahci: port setup failed\n");
        return -1;
    }

    if (ahci_identify_device(&ahci0_context, identify_words) != 0) {
        console_write("ahci: identify failed\n");
        return -1;
    }

    sector_count_28 = (uint32_t)identify_words[60] | ((uint32_t)identify_words[61] << 16);
    sector_count_48 = (uint64_t)identify_words[100] |
                      ((uint64_t)identify_words[101] << 16) |
                      ((uint64_t)identify_words[102] << 32) |
                      ((uint64_t)identify_words[103] << 48);

    if (sector_count_48 != 0u && sector_count_48 <= 0xFFFFFFFFu) {
        sector_count = sector_count_48;
    } else {
        sector_count = sector_count_28;
    }

    if (sector_count == 0u || sector_count > 0xFFFFFFFFu) {
        console_write("ahci: identify word60 0x");
        console_write_hex32(identify_words[60]);
        console_write(" word61 0x");
        console_write_hex32(identify_words[61]);
        console_write(" word100 0x");
        console_write_hex32(identify_words[100]);
        console_write(" word101 0x");
        console_write_hex32(identify_words[101]);
        console_write(" word102 0x");
        console_write_hex32(identify_words[102]);
        console_write(" word103 0x");
        console_write_hex32(identify_words[103]);
        console_write("\n");
        console_write("ahci: invalid sector count\n");
        return -1;
    }

    ahci0_controller.name = "ahci-ctl0";
    ahci0_controller.transport = BLOCK_TRANSPORT_AHCI;
    ahci0_controller.context = &ahci0_context;
    if (block_register_controller(&ahci0_controller) != 0) {
        console_write("ahci: failed to register controller\n");
        return -1;
    }

    ahci0_device.name = "ahci0";
    ahci0_device.block_size = 512u;
    ahci0_device.block_count = (uint32_t)sector_count;
    ahci0_device.read_only = 0u;
    ahci0_device.read = ahci_read;
    ahci0_device.write = ahci_write;
    ahci0_device.context = &ahci0_context;
    ahci0_device.controller = &ahci0_controller;
    ahci0_device.parent = 0;

    if (block_register(&ahci0_device) != 0) {
        console_write("ahci: failed to register ahci0\n");
        return -1;
    }

    console_write("ahci: detected ahci0 port 0x");
    console_write_hex32(ahci0_context.port_index);
    console_write(" sectors 0x");
    console_write_hex32((uint32_t)sector_count);
    console_write("\n");
    return 0;
}
