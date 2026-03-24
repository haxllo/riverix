#include "kernel/pit.h"

#include <stdint.h>

#include "kernel/console.h"
#include "kernel/io.h"

enum {
    PIT_COMMAND = 0x43,
    PIT_CHANNEL0 = 0x40,
    PIT_BASE_FREQUENCY = 1193182u,
    PIT_COMMAND_CHANNEL0 = 0x00u,
    PIT_COMMAND_LOHI = 0x30u,
    PIT_COMMAND_MODE2 = 0x04u,
};

static volatile uint32_t tick_count;
static volatile uint32_t hardware_tick_seen;
static uint32_t fallback_announced;

void pit_init(uint32_t frequency_hz) {
    uint32_t divisor = PIT_BASE_FREQUENCY / frequency_hz;

    if (divisor == 0u) {
        divisor = 1u;
    }

    outb(PIT_COMMAND, PIT_COMMAND_CHANNEL0 | PIT_COMMAND_LOHI | PIT_COMMAND_MODE2);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFFu));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFFu));

    console_write("pit: configured\n");
}

void pit_handle_tick(void) {
    hardware_tick_seen = 1u;
    tick_count++;

    if ((tick_count % 25u) == 0u) {
        console_write("pit: tick 0x");
        console_write_hex32(tick_count);
        console_write("\n");
    }
}

void pit_ensure_progress(void) {
    if (hardware_tick_seen != 0u) {
        return;
    }

    if (fallback_announced == 0u) {
        fallback_announced = 1u;
        console_write("pit: synthetic fallback\n");
    }

    tick_count++;
}

uint32_t pit_ticks(void) {
    return tick_count;
}
