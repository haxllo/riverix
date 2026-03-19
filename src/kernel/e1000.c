#include "kernel/e1000.h"

#include <stdint.h>

#include "kernel/console.h"
#include "kernel/mmio.h"
#include "kernel/paging.h"
#include "kernel/palloc.h"
#include "kernel/pci.h"

#define E1000_CLASS_CODE 0x02u
#define E1000_SUBCLASS 0x00u
#define E1000_PROG_IF 0x00u
#define E1000_BAR_INDEX 0u
#define E1000_MMIO_BYTES 0x20000u
#define E1000_TX_RING_COUNT 16u
#define E1000_RX_RING_COUNT 32u
#define E1000_BUFFER_BYTES 2048u
#define E1000_FRAME_MIN_BYTES 60u

#define E1000_REG_CTRL 0x0000u
#define E1000_REG_STATUS 0x0008u
#define E1000_REG_IMC 0x00D8u
#define E1000_REG_RCTL 0x0100u
#define E1000_REG_TCTL 0x0400u
#define E1000_REG_TIPG 0x0410u
#define E1000_REG_RDBAL 0x2800u
#define E1000_REG_RDBAH 0x2804u
#define E1000_REG_RDLEN 0x2808u
#define E1000_REG_RDH 0x2810u
#define E1000_REG_RDT 0x2818u
#define E1000_REG_TDBAL 0x3800u
#define E1000_REG_TDBAH 0x3804u
#define E1000_REG_TDLEN 0x3808u
#define E1000_REG_TDH 0x3810u
#define E1000_REG_TDT 0x3818u
#define E1000_REG_RAL0 0x5400u
#define E1000_REG_RAH0 0x5404u

#define E1000_CTRL_SLU (1u << 6)
#define E1000_CTRL_RST (1u << 26)

#define E1000_RCTL_EN (1u << 1)
#define E1000_RCTL_BAM (1u << 15)
#define E1000_RCTL_SECRC (1u << 26)

#define E1000_TCTL_EN (1u << 1)
#define E1000_TCTL_PSP (1u << 3)
#define E1000_TCTL_CT_SHIFT 4u
#define E1000_TCTL_COLD_SHIFT 12u

#define E1000_TXD_CMD_EOP 0x01u
#define E1000_TXD_CMD_RS 0x08u
#define E1000_TXD_STAT_DD 0x01u

#define E1000_RXD_STAT_DD 0x01u
#define E1000_RXD_STAT_EOP 0x02u

#define E1000_RESET_POLL_LIMIT 100000u

typedef struct e1000_tx_desc {
    uint64_t address;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

typedef struct e1000_rx_desc {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

typedef struct e1000_context {
    pci_address_t pci;
    volatile uint32_t *registers;
    e1000_tx_desc_t *tx_ring;
    e1000_rx_desc_t *rx_ring;
    uint32_t tx_ring_phys;
    uint32_t rx_ring_phys;
    uint32_t tx_buffer_phys[E1000_TX_RING_COUNT];
    uint32_t rx_buffer_phys[E1000_RX_RING_COUNT];
    uint32_t rx_tail;
    uint8_t mac[6];
    uint32_t ready;
    uint32_t tx_packets;
    uint32_t rx_packets;
} e1000_context_t;

static e1000_context_t e1000_state;

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

static uint32_t e1000_read_reg(uint32_t offset) {
    return e1000_state.registers[offset / sizeof(uint32_t)];
}

static void e1000_write_reg(uint32_t offset, uint32_t value) {
    e1000_state.registers[offset / sizeof(uint32_t)] = value;
}

static int e1000_wait_reset_complete(void) {
    uint32_t attempts;

    for (attempts = 0u; attempts < E1000_RESET_POLL_LIMIT; attempts++) {
        if ((e1000_read_reg(E1000_REG_CTRL) & E1000_CTRL_RST) == 0u) {
            return 0;
        }
    }

    return -1;
}

static int e1000_alloc_rings(void) {
    uint32_t index;

    e1000_state.tx_ring_phys = palloc_alloc_page();
    e1000_state.rx_ring_phys = palloc_alloc_page();
    if (e1000_state.tx_ring_phys == 0u || e1000_state.rx_ring_phys == 0u) {
        return -1;
    }

    e1000_state.tx_ring = (e1000_tx_desc_t *)paging_phys_to_virt(e1000_state.tx_ring_phys);
    e1000_state.rx_ring = (e1000_rx_desc_t *)paging_phys_to_virt(e1000_state.rx_ring_phys);
    zero_bytes(e1000_state.tx_ring, PAGE_SIZE);
    zero_bytes(e1000_state.rx_ring, PAGE_SIZE);

    for (index = 0u; index < E1000_TX_RING_COUNT; index++) {
        e1000_state.tx_buffer_phys[index] = palloc_alloc_page();
        if (e1000_state.tx_buffer_phys[index] == 0u) {
            return -1;
        }

        zero_bytes((void *)paging_phys_to_virt(e1000_state.tx_buffer_phys[index]), PAGE_SIZE);
        e1000_state.tx_ring[index].address = (uint64_t)e1000_state.tx_buffer_phys[index];
        e1000_state.tx_ring[index].status = E1000_TXD_STAT_DD;
    }

    for (index = 0u; index < E1000_RX_RING_COUNT; index++) {
        e1000_state.rx_buffer_phys[index] = palloc_alloc_page();
        if (e1000_state.rx_buffer_phys[index] == 0u) {
            return -1;
        }

        zero_bytes((void *)paging_phys_to_virt(e1000_state.rx_buffer_phys[index]), PAGE_SIZE);
        e1000_state.rx_ring[index].address = (uint64_t)e1000_state.rx_buffer_phys[index];
        e1000_state.rx_ring[index].status = 0u;
    }

    return 0;
}

static void e1000_program_tx(void) {
    e1000_write_reg(E1000_REG_TDBAL, e1000_state.tx_ring_phys);
    e1000_write_reg(E1000_REG_TDBAH, 0u);
    e1000_write_reg(E1000_REG_TDLEN, E1000_TX_RING_COUNT * (uint32_t)sizeof(e1000_tx_desc_t));
    e1000_write_reg(E1000_REG_TDH, 0u);
    e1000_write_reg(E1000_REG_TDT, 0u);
    e1000_write_reg(E1000_REG_TCTL,
                    E1000_TCTL_EN |
                    E1000_TCTL_PSP |
                    (0x10u << E1000_TCTL_CT_SHIFT) |
                    (0x40u << E1000_TCTL_COLD_SHIFT));
    e1000_write_reg(E1000_REG_TIPG, 10u | (8u << 10) | (6u << 20));
}

static void e1000_program_rx(void) {
    e1000_state.rx_tail = E1000_RX_RING_COUNT - 1u;
    e1000_write_reg(E1000_REG_RDBAL, e1000_state.rx_ring_phys);
    e1000_write_reg(E1000_REG_RDBAH, 0u);
    e1000_write_reg(E1000_REG_RDLEN, E1000_RX_RING_COUNT * (uint32_t)sizeof(e1000_rx_desc_t));
    e1000_write_reg(E1000_REG_RDH, 0u);
    e1000_write_reg(E1000_REG_RDT, E1000_RX_RING_COUNT - 1u);
    e1000_write_reg(E1000_REG_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);
}

static void e1000_read_mac(uint8_t mac[6]) {
    uint32_t low = e1000_read_reg(E1000_REG_RAL0);
    uint32_t high = e1000_read_reg(E1000_REG_RAH0);

    mac[0] = (uint8_t)(low & 0xFFu);
    mac[1] = (uint8_t)((low >> 8) & 0xFFu);
    mac[2] = (uint8_t)((low >> 16) & 0xFFu);
    mac[3] = (uint8_t)((low >> 24) & 0xFFu);
    mac[4] = (uint8_t)(high & 0xFFu);
    mac[5] = (uint8_t)((high >> 8) & 0xFFu);
}

int32_t e1000_init(void) {
    pci_address_t address;
    uint32_t bar0;
    uintptr_t mmio_base;

    if (e1000_state.ready != 0u) {
        return 0;
    }

    pci_init();
    if (pci_find_class(E1000_CLASS_CODE, E1000_SUBCLASS, E1000_PROG_IF, &address) != 0) {
        console_write("e1000: no controller\n");
        return -1;
    }

    bar0 = pci_read_bar(address, E1000_BAR_INDEX);
    if ((bar0 & 0x1u) != 0u) {
        console_write("e1000: io bars unsupported\n");
        return -1;
    }

    mmio_base = mmio_map_region(bar0 & 0xFFFFFFF0u, E1000_MMIO_BYTES);
    if (mmio_base == 0u) {
        console_write("e1000: mmio map failed\n");
        return -1;
    }

    zero_bytes(&e1000_state, sizeof(e1000_state));
    e1000_state.pci = address;
    e1000_state.registers = (volatile uint32_t *)mmio_base;

    pci_enable_memory_space(address);
    pci_enable_bus_master(address);

    e1000_write_reg(E1000_REG_IMC, 0xFFFFFFFFu);
    e1000_write_reg(E1000_REG_CTRL, e1000_read_reg(E1000_REG_CTRL) | E1000_CTRL_RST);
    if (e1000_wait_reset_complete() != 0) {
        console_write("e1000: reset timeout\n");
        return -1;
    }

    e1000_write_reg(E1000_REG_IMC, 0xFFFFFFFFu);
    e1000_write_reg(E1000_REG_CTRL, e1000_read_reg(E1000_REG_CTRL) | E1000_CTRL_SLU);

    if (e1000_alloc_rings() != 0) {
        console_write("e1000: dma setup failed\n");
        return -1;
    }

    e1000_program_tx();
    e1000_program_rx();
    e1000_read_mac(e1000_state.mac);
    e1000_state.ready = 1u;

    console_write("e1000: ready\n");
    return 0;
}

int e1000_ready(void) {
    return e1000_state.ready != 0u;
}

int32_t e1000_get_mac(uint8_t mac[6]) {
    if (mac == 0 || e1000_state.ready == 0u) {
        return -1;
    }

    copy_bytes(mac, e1000_state.mac, 6u);
    return 0;
}

int32_t e1000_transmit(const void *frame, uint32_t length) {
    uint32_t tail;
    uint32_t tx_length;
    e1000_tx_desc_t *descriptor;
    uint8_t *tx_buffer;

    if (frame == 0 || length == 0u || length > E1000_BUFFER_BYTES || e1000_state.ready == 0u) {
        return -1;
    }

    tail = e1000_read_reg(E1000_REG_TDT) % E1000_TX_RING_COUNT;
    descriptor = &e1000_state.tx_ring[tail];
    if ((descriptor->status & E1000_TXD_STAT_DD) == 0u) {
        return -1;
    }

    tx_length = length;
    if (tx_length < E1000_FRAME_MIN_BYTES) {
        tx_length = E1000_FRAME_MIN_BYTES;
    }

    tx_buffer = (uint8_t *)paging_phys_to_virt(e1000_state.tx_buffer_phys[tail]);
    zero_bytes(tx_buffer, tx_length);
    copy_bytes(tx_buffer, frame, length);

    descriptor->length = (uint16_t)tx_length;
    descriptor->cso = 0u;
    descriptor->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
    descriptor->status = 0u;
    descriptor->css = 0u;
    descriptor->special = 0u;

    e1000_write_reg(E1000_REG_TDT, (tail + 1u) % E1000_TX_RING_COUNT);
    e1000_state.tx_packets++;
    return (int32_t)length;
}

int32_t e1000_receive(void *frame, uint32_t capacity) {
    uint32_t index;
    uint32_t length;
    e1000_rx_desc_t *descriptor;
    uint8_t *rx_buffer;

    if (frame == 0 || capacity == 0u || e1000_state.ready == 0u) {
        return -1;
    }

    index = (e1000_state.rx_tail + 1u) % E1000_RX_RING_COUNT;
    descriptor = &e1000_state.rx_ring[index];
    if ((descriptor->status & E1000_RXD_STAT_DD) == 0u) {
        return 0;
    }

    length = descriptor->length;
    if ((descriptor->status & E1000_RXD_STAT_EOP) == 0u ||
        length == 0u ||
        length > capacity) {
        descriptor->status = 0u;
        descriptor->length = 0u;
        e1000_state.rx_tail = index;
        e1000_write_reg(E1000_REG_RDT, index);
        return -1;
    }

    rx_buffer = (uint8_t *)paging_phys_to_virt(e1000_state.rx_buffer_phys[index]);
    copy_bytes(frame, rx_buffer, length);
    descriptor->status = 0u;
    descriptor->length = 0u;
    e1000_state.rx_tail = index;
    e1000_write_reg(E1000_REG_RDT, index);
    e1000_state.rx_packets++;
    return (int32_t)length;
}
