#ifndef RIVERIX_MMIO_H
#define RIVERIX_MMIO_H

#include <stdint.h>

void mmio_init(void);
uintptr_t mmio_map_region(uint32_t physical_address, uint32_t length);

#endif
