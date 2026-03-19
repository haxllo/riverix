#ifndef RIVERIX_E1000_H
#define RIVERIX_E1000_H

#include <stdint.h>

int32_t e1000_init(void);
int e1000_ready(void);
int32_t e1000_get_mac(uint8_t mac[6]);
int32_t e1000_transmit(const void *frame, uint32_t length);
int32_t e1000_receive(void *frame, uint32_t capacity);

#endif
