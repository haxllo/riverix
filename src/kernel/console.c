#include "kernel/console.h"

#include <stdint.h>

#include "kernel/serial.h"
#include "kernel/vga.h"

static char hex_digit(uint8_t value) {
    if (value < 10) {
        return (char)('0' + value);
    }

    return (char)('A' + (value - 10));
}

static uint32_t string_length(const char *message) {
    uint32_t length = 0u;

    if (message == 0) {
        return 0u;
    }

    while (message[length] != '\0') {
        length++;
    }

    return length;
}

static uint32_t console_irq_save(void) {
    uint32_t flags;

    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void console_irq_restore(uint32_t flags) {
    if ((flags & 0x200u) != 0u) {
        __asm__ volatile ("sti" : : : "memory");
    }
}

void console_init(void) {
    serial_init();
    vga_init();
}

void console_write_len(const char *message, uint32_t length) {
    uint32_t index;
    uint32_t flags;

    if (message == 0 || length == 0u) {
        return;
    }

    flags = console_irq_save();

    for (index = 0u; index < length; index++) {
        serial_write_char(message[index]);
        vga_write_char(message[index]);
    }

    console_irq_restore(flags);
}

void console_write(const char *message) {
    console_write_len(message, string_length(message));
}

void console_write_hex32(uint32_t value) {
    char buffer[8];
    int shift;
    uint32_t index = 0u;

    for (shift = 28; shift >= 0; shift -= 4) {
        uint8_t nibble = (uint8_t)((value >> shift) & 0xF);
        buffer[index++] = hex_digit(nibble);
    }

    console_write_len(buffer, sizeof(buffer));
}

void console_write_hex64(uint64_t value) {
    char buffer[16];
    int shift;
    uint32_t index = 0u;

    for (shift = 60; shift >= 0; shift -= 4) {
        uint8_t nibble = (uint8_t)((value >> shift) & 0xFu);
        buffer[index++] = hex_digit(nibble);
    }

    console_write_len(buffer, sizeof(buffer));
}
