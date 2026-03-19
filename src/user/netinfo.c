#include <stdint.h>

#include "user/net.h"
#include "user/unistd.h"

#define NETINFO_BUFFER_CAPACITY 192u

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

static void append_hex2(char *buffer, uint32_t capacity, uint32_t *length, uint8_t value) {
    static const char digits[] = "0123456789ABCDEF";

    append_char(buffer, capacity, length, digits[(value >> 4) & 0xFu]);
    append_char(buffer, capacity, length, digits[value & 0xFu]);
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

static void append_mac(char *buffer, uint32_t capacity, uint32_t *length, const uint8_t mac[6]) {
    uint32_t index;

    for (index = 0u; index < 6u; index++) {
        if (index != 0u) {
            append_char(buffer, capacity, length, ':');
        }

        append_hex2(buffer, capacity, length, mac[index]);
    }
}

int main(int argc, char **argv) {
    netinfo_t info;
    char line[NETINFO_BUFFER_CAPACITY];
    uint32_t length = 0u;

    (void)argc;
    (void)argv;

    if (getnetinfo(&info) != 0 || info.ready == 0u) {
        (void)write_all(2, "netinfo: unavailable\n", 21u);
        return 1;
    }

    line[0] = '\0';
    append_str(line, sizeof(line), &length, "netinfo: ready mac ");
    append_mac(line, sizeof(line), &length, info.mac);
    append_str(line, sizeof(line), &length, " ipv4 ");
    append_ipv4(line, sizeof(line), &length, info.ipv4_addr);
    append_str(line, sizeof(line), &length, " netmask ");
    append_ipv4(line, sizeof(line), &length, info.ipv4_netmask);
    append_str(line, sizeof(line), &length, " gateway ");
    append_ipv4(line, sizeof(line), &length, info.ipv4_gateway);
    append_char(line, sizeof(line), &length, '\n');

    return write_all(1, line, length) == 0 ? 0 : 1;
}
