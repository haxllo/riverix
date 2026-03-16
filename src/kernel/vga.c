#include "kernel/vga.h"

#include <stddef.h>
#include <stdint.h>

enum {
    VGA_WIDTH = 80,
    VGA_HEIGHT = 25,
    VGA_COLOR = 0x0F,
};

static uint16_t *const vga_buffer = (uint16_t *)0xB8000;
static size_t cursor_row = 0;
static size_t cursor_column = 0;

static void vga_put_entry_at(char ch, uint8_t color, size_t row, size_t column) {
    const size_t index = row * VGA_WIDTH + column;
    vga_buffer[index] = (uint16_t)ch | ((uint16_t)color << 8);
}

static void vga_clear_row(size_t row) {
    size_t column;

    for (column = 0; column < VGA_WIDTH; column++) {
        vga_put_entry_at(' ', VGA_COLOR, row, column);
    }
}

static void vga_scroll(void) {
    size_t row;
    size_t column;

    for (row = 1; row < VGA_HEIGHT; row++) {
        for (column = 0; column < VGA_WIDTH; column++) {
            vga_buffer[(row - 1) * VGA_WIDTH + column] = vga_buffer[row * VGA_WIDTH + column];
        }
    }

    vga_clear_row(VGA_HEIGHT - 1);
}

static void vga_newline(void) {
    cursor_column = 0;

    if (cursor_row < VGA_HEIGHT - 1) {
        cursor_row++;
        return;
    }

    vga_scroll();
}

void vga_init(void) {
    size_t row;

    cursor_row = 0;
    cursor_column = 0;

    for (row = 0; row < VGA_HEIGHT; row++) {
        vga_clear_row(row);
    }
}

void vga_write_char(char ch) {
    if (ch == '\n') {
        vga_newline();
        return;
    }

    vga_put_entry_at(ch, VGA_COLOR, cursor_row, cursor_column);
    cursor_column++;

    if (cursor_column >= VGA_WIDTH) {
        vga_newline();
    }
}
