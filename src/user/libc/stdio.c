#include <stdint.h>

#include "user/stdio.h"
#include "user/string.h"
#include "user/unistd.h"

static int32_t write_all(int32_t fd, const char *buffer, uint32_t length) {
    uint32_t written = 0u;

    while (written < length) {
        int32_t result = write(fd, buffer + written, length - written);

        if (result <= 0) {
            return -1;
        }

        written += (uint32_t)result;
    }

    return (int32_t)written;
}

int32_t putstr_fd(int32_t fd, const char *text) {
    return write_all(fd, text, (uint32_t)strlen(text));
}

int32_t puts(const char *text) {
    if (putstr_fd(1, text) < 0) {
        return -1;
    }

    return write_all(1, "\n", 1u);
}

int32_t puthex32_fd(int32_t fd, uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char buffer[8];
    uint32_t index;

    for (index = 0u; index < 8u; index++) {
        buffer[index] = digits[(value >> ((7u - index) * 4u)) & 0xFu];
    }

    return write_all(fd, buffer, sizeof(buffer));
}

int32_t puthex32(uint32_t value) {
    return puthex32_fd(1, value);
}

int32_t readline_fd(int32_t fd, char *buffer, uint32_t capacity) {
    uint32_t count = 0u;

    if (buffer == 0 || capacity == 0u) {
        return -1;
    }

    while (count + 1u < capacity) {
        char ch;
        int32_t result = read(fd, &ch, 1u);

        if (result < 0) {
            return -1;
        }

        if (result == 0) {
            break;
        }

        buffer[count++] = ch;
        if (ch == '\n') {
            break;
        }
    }

    buffer[count] = '\0';
    return (int32_t)count;
}
