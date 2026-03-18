#ifndef RIVERIX_SERIAL_H
#define RIVERIX_SERIAL_H

void serial_init(void);
void serial_write_char(char ch);
int serial_can_read(void);
char serial_read_char(void);

#endif
