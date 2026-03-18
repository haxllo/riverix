#include "kernel/serial.h"

#include <stdint.h>

#include "kernel/io.h"

enum {
    COM1 = 0x3F8,
};

static int serial_tx_ready(void) {
    return (inb(COM1 + 5) & 0x20) != 0;
}

int serial_can_read(void) {
    return (inb(COM1 + 5) & 0x01) != 0;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
    io_wait();
}

void serial_write_char(char ch) {
    while (!serial_tx_ready()) {
    }

    outb(COM1, (uint8_t)ch);
}

char serial_read_char(void) {
    while (!serial_can_read()) {
    }

    return (char)inb(COM1);
}
