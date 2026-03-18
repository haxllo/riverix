#ifndef RIVERIX_USER_STRING_H
#define RIVERIX_USER_STRING_H

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *destination, const void *source, size_t length);
void *memset(void *destination, int value, size_t length);
int memcmp(const void *left, const void *right, size_t length);
size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t length);
char *strcpy(char *destination, const char *source);

#endif
