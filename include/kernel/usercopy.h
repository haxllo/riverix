#ifndef RIVERIX_USERCOPY_H
#define RIVERIX_USERCOPY_H

#include <stdint.h>

int user_copy_from_in(uint32_t directory_phys, void *destination, uint32_t source_address, uint32_t length);
int user_copy_to_in(uint32_t directory_phys, uint32_t destination_address, const void *source, uint32_t length);
int user_copy_string_from_in(uint32_t directory_phys, char *destination, uint32_t source_address, uint32_t max_length);
int user_copy_from(void *destination, uint32_t source_address, uint32_t length);
int user_copy_to(uint32_t destination_address, const void *source, uint32_t length);
int user_copy_string_from(char *destination, uint32_t source_address, uint32_t max_length);

#endif
