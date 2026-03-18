#ifndef RIVERIX_PCI_H
#define RIVERIX_PCI_H

#include <stdint.h>

typedef struct pci_address {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
} pci_address_t;

void pci_init(void);
uint32_t pci_config_read32(pci_address_t address, uint8_t offset);
uint16_t pci_config_read16(pci_address_t address, uint8_t offset);
uint8_t pci_config_read8(pci_address_t address, uint8_t offset);
int pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_address_t *out_address);
uint32_t pci_read_bar(pci_address_t address, uint8_t bar_index);
void pci_enable_bus_master(pci_address_t address);
void pci_enable_memory_space(pci_address_t address);

#endif
