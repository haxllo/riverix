#include <stddef.h>

#include "user/string.h"

void *memcpy(void *destination, const void *source, size_t length) {
    unsigned char *dst = (unsigned char *)destination;
    const unsigned char *src = (const unsigned char *)source;
    size_t index;

    for (index = 0; index < length; index++) {
        dst[index] = src[index];
    }

    return destination;
}

void *memset(void *destination, int value, size_t length) {
    unsigned char *dst = (unsigned char *)destination;
    size_t index;

    for (index = 0; index < length; index++) {
        dst[index] = (unsigned char)value;
    }

    return destination;
}

int memcmp(const void *left, const void *right, size_t length) {
    const unsigned char *lhs = (const unsigned char *)left;
    const unsigned char *rhs = (const unsigned char *)right;
    size_t index;

    for (index = 0; index < length; index++) {
        if (lhs[index] != rhs[index]) {
            return (int)lhs[index] - (int)rhs[index];
        }
    }

    return 0;
}

size_t strlen(const char *text) {
    size_t length = 0;

    while (text[length] != '\0') {
        length++;
    }

    return length;
}

int strcmp(const char *left, const char *right) {
    size_t index = 0;

    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return (int)(unsigned char)left[index] - (int)(unsigned char)right[index];
        }

        index++;
    }

    return (int)(unsigned char)left[index] - (int)(unsigned char)right[index];
}

int strncmp(const char *left, const char *right, size_t length) {
    size_t index;

    for (index = 0; index < length; index++) {
        if (left[index] != right[index] || left[index] == '\0' || right[index] == '\0') {
            return (int)(unsigned char)left[index] - (int)(unsigned char)right[index];
        }
    }

    return 0;
}

char *strcpy(char *destination, const char *source) {
    size_t index = 0;

    while (source[index] != '\0') {
        destination[index] = source[index];
        index++;
    }

    destination[index] = '\0';
    return destination;
}
