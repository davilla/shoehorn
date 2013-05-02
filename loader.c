/*
 * loader.c --		Target-side bootstrap loader for CL-PS7111, EP7211.
 *
 * Copyright (C) 1999 Ben Williamson <benw@pobox.com>
 *
 * This program is loaded into SRAM in bootstrap mode, where it waits
 * for commands on UART1 to read and write memory, jump to code etc.
 * A design goal for this program is to be entirely independent of the
 * target board.  Anything with a CL-PS7111 or EP7211 should be able to run
 * this code in bootstrap mode.  All the board specifics can be handled on
 * the host.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "ioregs.h"

#define DRAM_START	((unsigned *)0xc0000000)
#define DRAM_END	((unsigned *)0xe0000000)
#define STEP_BYTES	(256*1024)
#define STEP_WORDS	(STEP_BYTES / 4)
#define PATTERN		0x12345678


extern void flush_v3(void);
extern void flush_v4(void);

int init=1;


static unsigned char get_char(void)
{
	while (IO_SYSFLG1 & URXFE1);
	return IO_UARTDR1 & 0xff;
}

static void drain(void)
{
	while (IO_SYSFLG1 & UTXFF1);
}

static void drain2(void)
{
	while (IO_SYSFLG2 & UTXFF2);
}

static void put_char(unsigned char c)
{
	drain();
	IO_UARTDR1 = c;
}

static unsigned char get_char2(void)
{
	unsigned char c;
	while (IO_SYSFLG2 & URXFE2);
	//return IO_UARTDR2 & 0xff;
	c = IO_UARTDR2 & 0xff;
	if (init) put_char(c);
	return c;
}

static void put_char2(unsigned char c)
{
	if (init) put_char(c);
	drain2();
	IO_UARTDR2 = c;
}

static unsigned get_word(void)
{
	unsigned w;
	
	w = get_char();
	w |= get_char() << 8;
	w |= get_char() << 16;
	w |= get_char() << 24;
	return w;
}

static void put_word(unsigned w)
{
	put_char(w);
	put_char(w >> 8);
	put_char(w >> 16);
	put_char(w >> 24);
}

/*
 * This is a destructive test for DRAM, which is safe because this program
 * runs entirely from internal SRAM.  First we detect the width of the DRAM,
 * which is reported by writing a single byte value of 16 or 32.  This is
 * followed by the compiled-in step size for scanning, which is 256kB.
 * 
 * We start at the end of the DRAM space and work backwards in 256kB steps,
 * writing the address of each block into its first word.  Then we scan
 * forward looking for blocks which contain their own address.  These are
 * the unique blocks of physical memory, which we report with word addresses.
 * The list is terminated with a zero address.
 */
static void detect_dram(void)
{
	volatile unsigned *p;

	IO_SYSCON2 &= ~DRAMSZ;		/* 32-bit wide */
	
	p = (unsigned *)DRAM_START;
	p[0] = PATTERN;
	p[1] = 0;			/* discharge data bus capacitance */
	if (p[0] != PATTERN) {
		IO_SYSCON2 |= DRAMSZ;	/* 16-bit wide it is then */
		put_char(16);
	} else {
		put_char(32);
	}
	put_word(STEP_BYTES);
	
	p = DRAM_END;
	while (p > DRAM_START) {
		p -= STEP_WORDS;
		*p = (unsigned)p;
	}
	/* p == DRAM_START */
	while (p < DRAM_END) {
		if (*p == (unsigned)p) {
			put_word((unsigned)p);
		}
		p+= STEP_WORDS;
	}
	put_word(0);
}


/*
 * Received:	start address
 *		length
 *
 * Transmitted:	data bytes
 *		checksum byte (sum of transmitted data)
 */
static void read_block(void)
{
	unsigned char checksum = 0;
	unsigned char *p = (unsigned char*) get_word();
	unsigned length = get_word();

	while (length--) {
		put_char(*p);
		checksum += *p++;
	}
	put_char(checksum);
}


/*
 * Received:	start address
 *		length
 *		data bytes...
 *
 * Transmitted:	checksum byte (sum of received data)
 */
static void write_block(void)
{
	unsigned char checksum = 0;
	unsigned char *p = (unsigned char*) get_word();
	unsigned length = get_word();

	while (length--) {
		*p = get_char();
		checksum += *p++;
	}
	put_char(checksum);
}

int flush_8051()
{
	char i=0;
	while ((IO_SYSFLG2 & URXFE2) != URXFE2) {
	  get_char2();
          i=1;
	}
	return i;
}

unsigned char write_8051(unsigned char c)
{
        put_char2(c);
       	return get_char2();
}

unsigned char read_51block(void)
{
	unsigned char hdr=0,i=1;
	hdr = get_char2();
	switch (hdr) {
		case 0x21:
			i=2;
			break;
		case 0x15:
			i=4;
			break;
		case 0x11:
			i=6;
			break;
		case 0x14:
		case 0x30:
			i=8;
			break;
	}
	while(--i)
		get_char2();
       	put_char2(0xce);
	return hdr;
}

int init_8051(void)
{

	IO_SYSCON2 = 0x00001140;
	if ((IO_SYSFLG2 & CKMODE) == CKMODE) {
		IO_UBRLCR2 = 0x000 + 0x00060000;
	} else {
		IO_UBRLCR2 = 0x001 + 0x00060000;
	}

	flush_8051();
        write_8051(0xf9);
	write_8051(0x10);
	write_8051(0x0c);
	write_8051(0x02);
	write_8051(0xFF);
	write_8051(0x00);
	write_8051(0x14);
	write_8051(0x05);
	write_8051(0xca);
	read_51block();
	read_51block();
	write_8051(0x13);
	write_8051(0x19);
	write_8051(0x00);
	write_8051(0x00);
	write_8051(0x00);
	write_8051(0xd4);
	if (read_51block() == 0x11)
		read_51block();
	read_51block();
	while (write_8051(0x7d) != 0xac) {
		flush_8051();
		put_char2(0xce);
	}
	write_8051(0x00);
	write_8051(0x0e);
	write_8051(0x75);
	init=0;
	return(1);
}


int
main(void)
{
	void (*code)(int r0, int r1, int r2, int r3);

	int r0, r1, r2, r3;
	volatile unsigned *w;
	volatile unsigned char *b;


	while (1) {
		if (init==0) {
			if (flush_8051())
				put_char2(0xce);
			write_8051(0x70);
			write_8051(0x90);
		}

		switch (get_char()) {
		case '3':	/* Flush v3 MMU */
			flush_v3();
			break;
			
		case '4':	/* Flush v4 MMU */
			flush_v4();
			break;
			
		case 'a':	/* Ack please */
			put_char('!');	/* we're fine, thanks */
			break;
			
		case 'c':	/* Call (address, r0, r1, r2, r3) */
			code = (void *)get_word();
			r0 = get_word();
			r1 = get_word();
			r2 = get_word();
			r3 = get_word();
			drain();
			code(r0, r1, r2, r3);
			break;
		
		case 'd':	/* Detect DRAM */
			detect_dram();
			break;
		
		case 'g':	/* Get single byte (address) */
			put_char(*(unsigned char *)get_word());
			break;

		case 'i':
			if (init)
				init_8051();
			put_char(0x00);
			put_char(0xFF);
			put_char(0x00);
			put_char(0xFF);
			break;
		
		case 's':	/* Set single byte (address, data) */
			b = (unsigned char *)get_word();
			*b = get_char();
			break;
		
		case 'r':	/* Read single word (address) */
			put_word(*(unsigned *)get_word());
			break;
		
		case 'w':	/* Write single word (address, data) */
			w = (unsigned *)get_word();
			*w = get_word();
			break;
		
		case 'R':	/* Read block */
			read_block();
			break;
		
		case 'W':	/* Write block */
			write_block();
			break;

		default:
			put_char('?');
			break;
		}
	}
}

