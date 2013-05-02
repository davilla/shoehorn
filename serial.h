/*
 * Copyright (c) 2000 Blue Mug, Inc.  All Rights Reserved.
 */
#ifndef _SHOEHORN_SERIAL_H
#define _SHOEHORN_SERIAL_H

#include <termios.h>

extern void serial_open(const char *dev);
extern void serial_close(void);
extern void serial_baud(speed_t speed);
extern void serial_terminal(void);

extern unsigned char get_char(void);
extern int get_char_timeout(int msecs);
extern void put_char(unsigned char c);
extern unsigned get_word(void);
extern void put_word(unsigned w);
extern void put_block(const char *buf, unsigned size);

extern unsigned char target_read_byte(unsigned addr);
extern void target_write_byte(unsigned addr, unsigned char data);
extern unsigned target_read_word(unsigned addr);
extern void target_write_word(unsigned addr, unsigned data);
extern void target_write_block(unsigned addr, const char *buf,
			       unsigned size, unsigned progress);

#endif /* _SHOEHORN_SERIAL_H */
