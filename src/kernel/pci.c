#include "kernel/pci.h"

#include <stdint.h>

#include "kernel/console.h"
#include "kernel/io.h"

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA 0xCFCu
#define PCI_COMMAND_OFFSET 0x04u
#define PCI_COMMAND_IO 0x1u
#define PCI_COMMAND_MEMORY 0x2u
#define PCI_COMMAND_BUS_MASTER 0x4u
#define PCI_VENDOR_DEVICE_OFFSET 0x00u
#define PCI_CLASS_REVISION_OFFSET 0x08u

static uint32_t pci_config_address(pci_address_t address, uint8_t offset) {
    return 0x80000000u |
           ((uint32_t)address.bus << 16) |
           ((uint32_t)address.slot << 11) |
           ((uint32_t)address.function << 8) |
           (offset & 0xFCu);
}

static uint32_t pci_initialized;

void pci_init(void) {
    if (pci_initialized != 0u) {
        return;
    }

    pci_initialized = 1u;
    console_write("pci: config io ready\n");
}

uint32_t pci_config_read32(pci_address_t address, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_config_address(address, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(pci_address_t address, uint8_t offset) {
    uint32_t value = pci_config_read32(address, offset);
    return (uint16_t)((value >> ((offset & 0x2u) * 8u)) & 0xFFFFu);
}

uint8_t pci_config_read8(pci_address_t address, uint8_t offset) {
    uint32_t value = pci_config_read32(address, offset);
    return (uint8_t)((value >> ((offset & 0x3u) * 8u)) & 0xFFu);
}

static void pci_config_write32(pci_address_t address, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_config_address(address, offset));
    outl(PCI_CONFIG_DATA, value);
}

int pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_address_t *out_address) {
    uint16_t bus;
    uint8_t slot;
    uint8_t function;

    for (bus = 0u; bus < 256u; bus++) {
        for (slot = 0u; slot < 32u; slot++) {
            for (function = 0u; function < 8u; function++) {
                pci_address_t address;
                uint32_t vendor_device;
                uint32_t class_revision;

                address.bus = (uint8_t)bus;
                address.slot = slot;
                address.function = function;

                vendor_device = pci_config_read32(address, PCI_VENDOR_DEVICE_OFFSET);
                if ((vendor_device & 0xFFFFu) == 0xFFFFu) {
                    continue;
                }

                class_revision = pci_config_read32(address, PCI_CLASS_REVISION_OFFSET);
                if ((uint8_t)(class_revision >> 24) != class_code ||
                    (uint8_t)(class_revision >> 16) != subclass ||
                    (uint8_t)(class_revision >> 8) != prog_if) {
                    continue;
                }

                if (out_address != 0) {
                    *out_address = address;
                }

                console_write("pci: found class 0x");
                console_write_hex32(((uint32_t)class_code << 16) | ((uint32_t)subclass << 8) | (uint32_t)prog_if);
                console_write(" at 0x");
                console_write_hex32(((uint32_t)address.bus << 16) | ((uint32_t)address.slot << 8) | address.function);
                console_write("\n");
                return 0;
            }
        }
    }

    return -1;
}

uint32_t pci_read_bar(pci_address_t address, uint8_t bar_index) {
    return pci_config_read32(address, (uint8_t)(0x10u + (bar_index * 4u)));
}

static void pci_set_command_bits(pci_address_t address, uint16_t bits) {
    uint32_t value = pci_config_read32(address, PCI_COMMAND_OFFSET);
    value |= bits;
    pci_config_write32(address, PCI_COMMAND_OFFSET, value);
}

void pci_enable_bus_master(pci_address_t address) {
    pci_set_command_bits(address, PCI_COMMAND_BUS_MASTER);
}

void pci_enable_memory_space(pci_address_t address) {
    pci_set_command_bits(address, PCI_COMMAND_MEMORY);
}
