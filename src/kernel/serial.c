#include "kernel/serial.h"

#include <stdint.h>

#include "kernel/io.h"

enum {
    COM1 = 0x3F8,
};

static int serial_present;

static int serial_tx_ready(void) {
    if (!serial_present) {
        return 0;
    }

    return (inb(COM1 + 5) & 0x20) != 0;
}

int serial_available(void) {
    return serial_present;
}

int serial_can_read(void) {
    if (!serial_present) {
        return 0;
    }

    return (inb(COM1 + 5) & 0x01) != 0;
}

void serial_init(void) {
    uint8_t probe_value;

    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x1Eu);
    outb(COM1 + 0, 0xAEu);
    io_wait();

    probe_value = inb(COM1 + 0);
    if (probe_value != 0xAEu) {
        serial_present = 0;
        outb(COM1 + 4, 0x00u);
        return;
    }

    serial_present = 1;
    outb(COM1 + 4, 0x0Bu);
}

void serial_write_char(char ch) {
    if (!serial_present) {
        return;
    }

    while (!serial_tx_ready()) {
    }

    outb(COM1, (uint8_t)ch);
}

char serial_read_char(void) {
    if (!serial_present) {
        return '\0';
    }

    while (!serial_can_read()) {
    }

    return (char)inb(COM1);
}
