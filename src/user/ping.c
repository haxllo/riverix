#include <stdint.h>

#include "user/net.h"
#include "user/unistd.h"

#define PING_BUFFER_CAPACITY 64u
#define PING_DEFAULT_TARGET 0x0A000202u
#define PING_DEFAULT_TIMEOUT 100u

static int32_t write_all(int32_t fd, const char *buffer, uint32_t length) {
    uint32_t written = 0u;

    while (written < length) {
        int32_t result = write(fd, buffer + written, length - written);

        if (result <= 0) {
            return -1;
        }

        written += (uint32_t)result;
    }

    return 0;
}

static void append_char(char *buffer, uint32_t capacity, uint32_t *length, char ch) {
    if (*length + 1u >= capacity) {
        return;
    }

    buffer[*length] = ch;
    (*length)++;
    buffer[*length] = '\0';
}

static void append_str(char *buffer, uint32_t capacity, uint32_t *length, const char *text) {
    uint32_t index = 0u;

    while (text[index] != '\0') {
        append_char(buffer, capacity, length, text[index]);
        index++;
    }
}

static void append_dec(char *buffer, uint32_t capacity, uint32_t *length, uint32_t value) {
    char digits[10];
    uint32_t count = 0u;

    if (value == 0u) {
        append_char(buffer, capacity, length, '0');
        return;
    }

    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (count != 0u) {
        append_char(buffer, capacity, length, digits[--count]);
    }
}

static void append_ipv4(char *buffer, uint32_t capacity, uint32_t *length, uint32_t ipv4_addr) {
    append_dec(buffer, capacity, length, (ipv4_addr >> 24) & 0xFFu);
    append_char(buffer, capacity, length, '.');
    append_dec(buffer, capacity, length, (ipv4_addr >> 16) & 0xFFu);
    append_char(buffer, capacity, length, '.');
    append_dec(buffer, capacity, length, (ipv4_addr >> 8) & 0xFFu);
    append_char(buffer, capacity, length, '.');
    append_dec(buffer, capacity, length, ipv4_addr & 0xFFu);
}

static int parse_ipv4(const char *text, uint32_t *out_ipv4) {
    uint32_t value = 0u;
    uint32_t octet = 0u;
    uint32_t octet_count = 0u;
    uint32_t index = 0u;

    if (text == 0 || out_ipv4 == 0) {
        return -1;
    }

    while (text[index] != '\0') {
        char ch = text[index];

        if (ch >= '0' && ch <= '9') {
            octet = (octet * 10u) + (uint32_t)(ch - '0');
            if (octet > 255u) {
                return -1;
            }
        } else if (ch == '.') {
            if (octet_count >= 3u) {
                return -1;
            }

            value = (value << 8) | octet;
            octet = 0u;
            octet_count++;
        } else {
            return -1;
        }

        index++;
    }

    if (octet_count != 3u) {
        return -1;
    }

    *out_ipv4 = (value << 8) | octet;
    return 0;
}

static int write_status(const char *prefix, uint32_t ipv4_addr) {
    char line[PING_BUFFER_CAPACITY];
    uint32_t length = 0u;

    line[0] = '\0';
    append_str(line, sizeof(line), &length, prefix);
    append_ipv4(line, sizeof(line), &length, ipv4_addr);
    append_char(line, sizeof(line), &length, '\n');
    return write_all(1, line, length);
}

int main(int argc, char **argv) {
    uint32_t target_ipv4 = PING_DEFAULT_TARGET;
    int32_t result;

    if (argc >= 2 && parse_ipv4(argv[1], &target_ipv4) != 0) {
        (void)write_all(2, "ping: invalid target\n", 21u);
        return 1;
    }

    result = ping4(target_ipv4, PING_DEFAULT_TIMEOUT);
    if (result == PING_OK) {
        return write_status("ping: ok ", target_ipv4) == 0 ? 0 : 1;
    }

    if (result == PING_ERR_TIMEOUT) {
        (void)write_status("ping: timeout ", target_ipv4);
        return 1;
    }

    if (result == PING_ERR_UNREACHABLE) {
        (void)write_status("ping: unreachable ", target_ipv4);
        return 1;
    }

    if (result == PING_ERR_BUSY) {
        (void)write_all(2, "ping: busy\n", 11u);
        return 1;
    }

    if (result == PING_ERR_NOT_READY) {
        (void)write_all(2, "ping: unavailable\n", 18u);
        return 1;
    }

    (void)write_all(2, "ping: failed\n", 13u);
    return 1;
}
