#ifndef RIVERIX_PIT_H
#define RIVERIX_PIT_H

#include <stdint.h>

void pit_init(uint32_t frequency_hz);
void pit_handle_tick(void);
void pit_ensure_progress(void);
uint32_t pit_ticks(void);

#endif
