#include "kernel/console.h"

#include "kernel/serial.h"
#include "kernel/vga.h"

static char hex_digit(uint8_t value) {
    if (value < 10) {
        return (char)('0' + value);
    }

    return (char)('A' + (value - 10));
}

void console_init(void) {
    serial_init();
    vga_init();
}

void console_write_len(const char *message, uint32_t length) {
    uint32_t index;

    for (index = 0u; index < length; index++) {
        serial_write_char(message[index]);
        vga_write_char(message[index]);
    }
}

void console_write(const char *message) {
    while (*message != '\0') {
        console_write_len(message, 1u);
        message++;
    }
}

void console_write_hex32(uint32_t value) {
    int shift;

    for (shift = 28; shift >= 0; shift -= 4) {
        uint8_t nibble = (uint8_t)((value >> shift) & 0xF);
        char ch = hex_digit(nibble);
        console_write_len(&ch, 1u);
    }
}

void console_write_hex64(uint64_t value) {
    console_write_hex32((uint32_t)(value >> 32));
    console_write_hex32((uint32_t)(value & 0xFFFFFFFFu));
}
