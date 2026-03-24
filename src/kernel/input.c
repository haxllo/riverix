#include "kernel/input.h"

#include <stdint.h>

#include "kernel/console.h"
#include "kernel/hyperv.h"
#include "kernel/hyperv_keyboard.h"
#include "kernel/io.h"
#include "kernel/platform.h"
#include "kernel/serial.h"

#define INPUT_QUEUE_CAPACITY 256u
#define I8042_DATA_PORT 0x60u
#define I8042_STATUS_PORT 0x64u
#define I8042_COMMAND_PORT 0x64u
#define I8042_STATUS_OUTPUT_FULL 0x01u
#define I8042_STATUS_INPUT_FULL 0x02u
#define I8042_STATUS_AUX_DATA 0x20u
#define I8042_ENABLE_FIRST_PORT 0xAEu
#define I8042_KEYBOARD_ENABLE_SCANNING 0xF4u
#define I8042_KEYBOARD_ACK 0xFAu
#define I8042_PREFIX_E0 0xE0u
#define I8042_PREFIX_E1 0xE1u
#define I8042_RELEASE_MASK 0x80u

static char input_queue[INPUT_QUEUE_CAPACITY];
static uint32_t input_queue_read_index;
static uint32_t input_queue_write_index;
static uint32_t input_queue_buffered;
static uint32_t input_backends;
static int i8042_shift;
static int i8042_caps_lock;
static int i8042_extended_prefix;

static uint32_t input_irq_save(void) {
    uint32_t flags;

    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void input_irq_restore(uint32_t flags) {
    if ((flags & 0x200u) != 0u) {
        __asm__ volatile ("sti" : : : "memory");
    }
}

static void input_queue_push_locked(char ch) {
    if (input_queue_buffered >= INPUT_QUEUE_CAPACITY) {
        return;
    }

    input_queue[input_queue_write_index] = ch;
    input_queue_write_index = (input_queue_write_index + 1u) % INPUT_QUEUE_CAPACITY;
    input_queue_buffered++;
}

void input_enqueue_char(char ch) {
    uint32_t flags = input_irq_save();

    input_queue_push_locked(ch);
    input_irq_restore(flags);
}

static int input_queue_pop_locked(char *out_ch) {
    if (out_ch == 0 || input_queue_buffered == 0u) {
        return 0;
    }

    *out_ch = input_queue[input_queue_read_index];
    input_queue_read_index = (input_queue_read_index + 1u) % INPUT_QUEUE_CAPACITY;
    input_queue_buffered--;
    return 1;
}

static int ascii_is_alpha(char ch) {
    return ch >= 'a' && ch <= 'z';
}

static char ascii_to_upper(char ch) {
    if (!ascii_is_alpha(ch)) {
        return ch;
    }

    return (char)(ch - ('a' - 'A'));
}

static int i8042_wait_input_clear(uint32_t attempts) {
    while (attempts-- != 0u) {
        if ((inb(I8042_STATUS_PORT) & I8042_STATUS_INPUT_FULL) == 0u) {
            return 1;
        }

        io_wait();
    }

    return 0;
}

static void i8042_flush_output(void) {
    uint32_t attempts = 1024u;

    while (attempts-- != 0u && (inb(I8042_STATUS_PORT) & I8042_STATUS_OUTPUT_FULL) != 0u) {
        (void)inb(I8042_DATA_PORT);
        io_wait();
    }
}

static void i8042_send_command(uint8_t command) {
    if (!i8042_wait_input_clear(4096u)) {
        return;
    }

    outb(I8042_COMMAND_PORT, command);
    io_wait();
}

static void i8042_send_data(uint8_t value) {
    if (!i8042_wait_input_clear(4096u)) {
        return;
    }

    outb(I8042_DATA_PORT, value);
    io_wait();
}

static char i8042_unshifted_char(uint8_t scancode) {
    static const char table[128] = {
        [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
        [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
        [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
        [0x0E] = '\b', [0x0F] = '\t',
        [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
        [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
        [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
        [0x1C] = '\n',
        [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
        [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
        [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
        [0x2B] = '\\',
        [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
        [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
        [0x34] = '.', [0x35] = '/',
        [0x39] = ' ',
    };

    return table[scancode & 0x7Fu];
}

static char i8042_shifted_char(uint8_t scancode) {
    static const char table[128] = {
        [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
        [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
        [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
        [0x0E] = '\b', [0x0F] = '\t',
        [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
        [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
        [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
        [0x1C] = '\n',
        [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F',
        [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
        [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
        [0x2B] = '|',
        [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V',
        [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
        [0x34] = '>', [0x35] = '?',
        [0x39] = ' ',
    };

    return table[scancode & 0x7Fu];
}

static char i8042_translate_char(uint8_t scancode) {
    char base = i8042_unshifted_char(scancode);

    if (base == '\0') {
        return '\0';
    }

    if (!ascii_is_alpha(base)) {
        return i8042_shift ? i8042_shifted_char(scancode) : base;
    }

    if ((i8042_shift ^ i8042_caps_lock) != 0) {
        return ascii_to_upper(base);
    }

    return base;
}

void input_process_xt_scancode(uint8_t scancode) {
    uint8_t code = scancode & (uint8_t)~I8042_RELEASE_MASK;
    int released = (scancode & I8042_RELEASE_MASK) != 0u;
    char ch;

    if (scancode == I8042_PREFIX_E0 || scancode == I8042_PREFIX_E1) {
        i8042_extended_prefix = 1;
        return;
    }

    if (i8042_extended_prefix != 0) {
        i8042_extended_prefix = 0;
        return;
    }

    if (code == 0x2Au || code == 0x36u) {
        i8042_shift = !released;
        return;
    }

    if (code == 0x3Au && !released) {
        i8042_caps_lock = !i8042_caps_lock;
        return;
    }

    if (released) {
        return;
    }

    ch = i8042_translate_char(code);
    if (ch == '\0') {
        return;
    }

    input_enqueue_char(ch);
}

static void input_poll_serial(void) {
    uint32_t flags;

    if ((input_backends & INPUT_BACKEND_SERIAL) == 0u) {
        return;
    }

    flags = input_irq_save();
    while (serial_can_read()) {
        input_queue_push_locked(serial_read_char());
    }
    input_irq_restore(flags);
}

static void input_poll_i8042(void) {
    if ((input_backends & INPUT_BACKEND_I8042) == 0u) {
        return;
    }

    for (;;) {
        uint8_t status = inb(I8042_STATUS_PORT);

        if ((status & I8042_STATUS_OUTPUT_FULL) == 0u) {
            break;
        }

        if ((status & I8042_STATUS_AUX_DATA) != 0u) {
            (void)inb(I8042_DATA_PORT);
            continue;
        }

        input_process_xt_scancode(inb(I8042_DATA_PORT));
    }
}

static void input_init_i8042(void) {
    uint8_t status = inb(I8042_STATUS_PORT);

    if (status == 0xFFu) {
        return;
    }

    i8042_flush_output();
    i8042_send_command(I8042_ENABLE_FIRST_PORT);
    i8042_send_data(I8042_KEYBOARD_ENABLE_SCANNING);
    if ((inb(I8042_STATUS_PORT) & I8042_STATUS_OUTPUT_FULL) != 0u) {
        uint8_t response = inb(I8042_DATA_PORT);

        if (response != I8042_KEYBOARD_ACK) {
            input_process_xt_scancode(response);
        }
    }

    input_backends |= INPUT_BACKEND_I8042;
}

void input_init(void) {
    input_queue_read_index = 0u;
    input_queue_write_index = 0u;
    input_queue_buffered = 0u;
    input_backends = 0u;
    i8042_shift = 0;
    i8042_caps_lock = 0;
    i8042_extended_prefix = 0;

    if (serial_available()) {
        input_backends |= INPUT_BACKEND_SERIAL;
    }

    input_init_i8042();
    if (hyperv_keyboard_init() != 0) {
        input_backends |= INPUT_BACKEND_HYPERV;
    }

    console_write("input: ready backends 0x");
    console_write_hex32(input_backends);
    console_write("\n");

    if (platform_is_hyperv() && input_backends == 0u) {
        if (hyperv_synic_ready()) {
            console_write("input: hyper-v vmbus ready, keyboard backend pending\n");
        } else {
            console_write("input: hyper-v transport pending\n");
        }
    }
}

void input_poll(void) {
    input_poll_serial();
    input_poll_i8042();
    hyperv_keyboard_poll();
}

int input_try_read_char(char *out_ch) {
    uint32_t flags;
    int result;

    input_poll();

    flags = input_irq_save();
    result = input_queue_pop_locked(out_ch);
    input_irq_restore(flags);

    return result;
}

uint32_t input_backend_flags(void) {
    return input_backends;
}

int input_has_backend(void) {
    return input_backends != 0u;
}
