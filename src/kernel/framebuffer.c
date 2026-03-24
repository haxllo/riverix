#include "kernel/framebuffer.h"

#include <stdint.h>

#include "kernel/console.h"
#include "kernel/framebuffer_font.h"
#include "kernel/mmio.h"
#include "kernel/multiboot.h"

static volatile uint8_t *framebuffer_base;
static uint32_t framebuffer_pitch;
static uint32_t framebuffer_width;
static uint32_t framebuffer_height;
static uint32_t framebuffer_bytes_per_pixel;
static uint32_t framebuffer_columns;
static uint32_t framebuffer_rows;
static uint32_t framebuffer_foreground;
static uint32_t framebuffer_background;
static uint32_t framebuffer_cursor_row;
static uint32_t framebuffer_cursor_column;
static uint8_t framebuffer_red_field_position;
static uint8_t framebuffer_red_mask_size;
static uint8_t framebuffer_green_field_position;
static uint8_t framebuffer_green_mask_size;
static uint8_t framebuffer_blue_field_position;
static uint8_t framebuffer_blue_mask_size;
static int framebuffer_ready;

static uint32_t scale_channel(uint8_t value, uint8_t mask_size) {
    uint32_t max_value;

    if (mask_size == 0u) {
        return 0u;
    }

    if (mask_size >= 8u) {
        return ((uint32_t)value) << (mask_size - 8u);
    }

    max_value = (1u << mask_size) - 1u;
    return ((uint32_t)value * max_value) / 255u;
}

static uint32_t framebuffer_pack_color(uint8_t red, uint8_t green, uint8_t blue) {
    uint32_t pixel = 0u;

    pixel |= scale_channel(red, framebuffer_red_mask_size) << framebuffer_red_field_position;
    pixel |= scale_channel(green, framebuffer_green_mask_size) << framebuffer_green_field_position;
    pixel |= scale_channel(blue, framebuffer_blue_mask_size) << framebuffer_blue_field_position;
    return pixel;
}

static void framebuffer_write_pixel(uint32_t x, uint32_t y, uint32_t pixel) {
    volatile uint8_t *address;
    uint32_t byte_index;

    address = framebuffer_base + (y * framebuffer_pitch) + (x * framebuffer_bytes_per_pixel);

    for (byte_index = 0u; byte_index < framebuffer_bytes_per_pixel; byte_index++) {
        address[byte_index] = (uint8_t)((pixel >> (byte_index * 8u)) & 0xFFu);
    }
}

static void framebuffer_clear_rows(uint32_t start_y, uint32_t row_count) {
    uint32_t y;
    uint32_t x;

    for (y = start_y; y < (start_y + row_count) && y < framebuffer_height; y++) {
        for (x = 0u; x < framebuffer_width; x++) {
            framebuffer_write_pixel(x, y, framebuffer_background);
        }
    }
}

static void framebuffer_draw_glyph(uint32_t cell_row, uint32_t cell_column, unsigned char ch) {
    uint32_t glyph_index;
    uint32_t pixel_y;
    uint32_t pixel_x;
    const uint8_t *glyph;

    glyph_index = (uint32_t)ch;
    if (glyph_index >= FRAMEBUFFER_FONT_GLYPHS) {
        glyph_index = (uint32_t)'?';
    }

    glyph = &framebuffer_font_8x16[glyph_index * FRAMEBUFFER_FONT_HEIGHT];
    pixel_y = cell_row * FRAMEBUFFER_FONT_HEIGHT;
    pixel_x = cell_column * FRAMEBUFFER_FONT_WIDTH;

    for (uint32_t row = 0u; row < FRAMEBUFFER_FONT_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        uint32_t y = pixel_y + row;
        uint32_t column;

        if (y >= framebuffer_height) {
            break;
        }

        for (column = 0u; column < FRAMEBUFFER_FONT_WIDTH; column++) {
            uint32_t x = pixel_x + column;
            uint32_t color = (bits & (uint8_t)(0x80u >> column)) != 0u
                                 ? framebuffer_foreground
                                 : framebuffer_background;

            if (x >= framebuffer_width) {
                break;
            }

            framebuffer_write_pixel(x, y, color);
        }
    }
}

static void framebuffer_scroll(void) {
    uint32_t bytes_to_move;
    uint32_t target_index;
    uint32_t source_index;

    if (!framebuffer_ready || framebuffer_height <= FRAMEBUFFER_FONT_HEIGHT) {
        return;
    }

    bytes_to_move = framebuffer_pitch * (framebuffer_height - FRAMEBUFFER_FONT_HEIGHT);
    for (target_index = 0u; target_index < bytes_to_move; target_index++) {
        source_index = target_index + (framebuffer_pitch * FRAMEBUFFER_FONT_HEIGHT);
        framebuffer_base[target_index] = framebuffer_base[source_index];
    }

    framebuffer_clear_rows(framebuffer_height - FRAMEBUFFER_FONT_HEIGHT, FRAMEBUFFER_FONT_HEIGHT);
}

static void framebuffer_newline(void) {
    framebuffer_cursor_column = 0u;

    if (framebuffer_cursor_row + 1u < framebuffer_rows) {
        framebuffer_cursor_row++;
        return;
    }

    framebuffer_scroll();
}

static void framebuffer_reset_state(void) {
    framebuffer_base = 0;
    framebuffer_pitch = 0u;
    framebuffer_width = 0u;
    framebuffer_height = 0u;
    framebuffer_bytes_per_pixel = 0u;
    framebuffer_columns = 0u;
    framebuffer_rows = 0u;
    framebuffer_foreground = 0u;
    framebuffer_background = 0u;
    framebuffer_cursor_row = 0u;
    framebuffer_cursor_column = 0u;
    framebuffer_red_field_position = 0u;
    framebuffer_red_mask_size = 0u;
    framebuffer_green_field_position = 0u;
    framebuffer_green_mask_size = 0u;
    framebuffer_blue_field_position = 0u;
    framebuffer_blue_mask_size = 0u;
    framebuffer_ready = 0;
}

void framebuffer_init(const boot_framebuffer_info_t *info) {
    uintptr_t virtual_base;
    uint64_t framebuffer_bytes;

    framebuffer_reset_state();

    if (info == 0 || info->present == 0u) {
        console_write("framebuffer: unavailable\n");
        return;
    }

    if (info->physical_address > 0xFFFFFFFFull) {
        console_write("framebuffer: address above 4GiB unsupported\n");
        return;
    }

    if (info->type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
        console_write("framebuffer: unsupported type 0x");
        console_write_hex32((uint32_t)info->type);
        console_write("\n");
        return;
    }

    framebuffer_bytes = (uint64_t)info->pitch * (uint64_t)info->height;
    if (framebuffer_bytes == 0u || framebuffer_bytes > 0xFFFFFFFFull) {
        console_write("framebuffer: invalid size\n");
        return;
    }

    framebuffer_bytes_per_pixel = ((uint32_t)info->bpp + 7u) / 8u;
    if (framebuffer_bytes_per_pixel < 2u || framebuffer_bytes_per_pixel > 4u) {
        console_write("framebuffer: unsupported bpp 0x");
        console_write_hex32((uint32_t)info->bpp);
        console_write("\n");
        return;
    }

    virtual_base = mmio_map_region((uint32_t)info->physical_address, (uint32_t)framebuffer_bytes);
    if (virtual_base == 0u) {
        console_write("framebuffer: map failed\n");
        return;
    }

    framebuffer_base = (volatile uint8_t *)virtual_base;
    framebuffer_pitch = info->pitch;
    framebuffer_width = info->width;
    framebuffer_height = info->height;
    framebuffer_red_field_position = info->red_field_position;
    framebuffer_red_mask_size = info->red_mask_size;
    framebuffer_green_field_position = info->green_field_position;
    framebuffer_green_mask_size = info->green_mask_size;
    framebuffer_blue_field_position = info->blue_field_position;
    framebuffer_blue_mask_size = info->blue_mask_size;
    framebuffer_columns = framebuffer_width / FRAMEBUFFER_FONT_WIDTH;
    framebuffer_rows = framebuffer_height / FRAMEBUFFER_FONT_HEIGHT;

    if (framebuffer_columns == 0u || framebuffer_rows == 0u) {
        console_write("framebuffer: mode too small for console\n");
        framebuffer_reset_state();
        return;
    }

    framebuffer_background = framebuffer_pack_color(0x10u, 0x12u, 0x16u);
    framebuffer_foreground = framebuffer_pack_color(0xE8u, 0xECu, 0xF1u);
    framebuffer_ready = 1;
    framebuffer_clear_rows(0u, framebuffer_height);

    console_write("framebuffer: ready ");
    console_write_hex32(framebuffer_width);
    console_write("x");
    console_write_hex32(framebuffer_height);
    console_write(" pitch 0x");
    console_write_hex32(framebuffer_pitch);
    console_write(" bpp 0x");
    console_write_hex32((uint32_t)info->bpp);
    console_write("\n");
}

int framebuffer_available(void) {
    return framebuffer_ready;
}

void framebuffer_write_char(char ch) {
    if (!framebuffer_ready) {
        return;
    }

    if (ch == '\r') {
        framebuffer_cursor_column = 0u;
        return;
    }

    if (ch == '\n') {
        framebuffer_newline();
        return;
    }

    if (ch == '\t') {
        uint32_t spaces = 4u - (framebuffer_cursor_column % 4u);

        for (uint32_t index = 0u; index < spaces; index++) {
            framebuffer_write_char(' ');
        }
        return;
    }

    framebuffer_draw_glyph(framebuffer_cursor_row, framebuffer_cursor_column, (unsigned char)ch);
    framebuffer_cursor_column++;

    if (framebuffer_cursor_column >= framebuffer_columns) {
        framebuffer_newline();
    }
}
