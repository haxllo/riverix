#ifndef RIVERIX_INPUT_H
#define RIVERIX_INPUT_H

#include <stdint.h>

enum {
    INPUT_BACKEND_SERIAL = 0x1u,
    INPUT_BACKEND_I8042 = 0x2u,
};

void input_init(void);
void input_poll(void);
int input_try_read_char(char *out_ch);
uint32_t input_backend_flags(void);
int input_has_backend(void);

#endif
