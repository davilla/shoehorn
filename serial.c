/*
 * Copyright (c) 2000 Blue Mug, Inc.  All Rights Reserved.
 * Copyright (c) 1999 Ben Williamson <benw@pobox.com>
 */

#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>

#include "serial.h"
#include "util.h"

#define SERIAL_BLOCKSIZE	0x1000

static struct termios oldtio, newtio, contio;
static int portfd = -1;

/* kill handler used when only the serial port is open */
static void handler1(int signal)
{
	assert(portfd >= 0);
	tcsetattr(portfd, TCSANOW, &oldtio);
	exit(1);
}

/* kill handler used in terminal mode */
static void handler2(int signal)
{
	assert(portfd >= 0);
	tcsetattr(portfd, TCSANOW, &oldtio);
	tcsetattr(STDIN_FILENO, TCSANOW, &contio);
	exit(1);
}

/* open the serial port */
void serial_open(const char *dev)
{
	portfd = open(dev, O_RDWR | O_NOCTTY);
	if (portfd < 0)
		perror_exit(dev);

	/* save current port settings */
	if (tcgetattr(portfd, &oldtio) < 0)
		perror_exit("tcgetattr");

	/* set signal handlers before switching settings */
	signal(SIGHUP, handler1);
	signal(SIGINT, handler1);
	signal(SIGPIPE, handler1);
	signal(SIGTERM, handler1);

	/* configure new port settings: 9600 8N1 */
	memset(&newtio, 0, sizeof(newtio));
	newtio.c_cflag = B9600 | CS8 | CLOCAL | CREAD;
	newtio.c_iflag = IGNPAR;
	newtio.c_oflag = 0;
	/* set input mode (non-canonical, no echo,...) */
	newtio.c_lflag = 0;
	newtio.c_cc[VTIME] = 0;	/* inter-character timer unused */
	newtio.c_cc[VMIN] = 1;	/* blocking read until 1 char received */

	/* install new port settings */
	tcflush(portfd, TCIFLUSH);
	if (tcsetattr(portfd, TCSANOW, &newtio) < 0)
		perror_exit("tcsetattr");
}

/* close serial port and restore settings */
void serial_close(void)
{
	assert(portfd >= 0);
	tcsetattr(portfd, TCSANOW, &oldtio);
	xclose(portfd);
	portfd = -1;
}

/* switch baud rate */
void serial_baud(speed_t speed)
{
	assert(portfd >= 0);
	tcdrain(portfd);
	usleep(50 * 1000);	/* 50 ms sleep; arbitrary */
	cfsetispeed(&newtio, speed);
	cfsetospeed(&newtio, speed);
	tcsetattr(portfd, TCSANOW, &newtio);
}

/* enter terminal mode */
void serial_terminal(void)
{
	struct termios tio;
		
	printf("Pretending to be a terminal - interrupt to exit.\n");
	tcgetattr(STDIN_FILENO, &contio);
	signal(SIGHUP, handler2);
	signal(SIGINT, handler2);
	signal(SIGPIPE, handler2);
	signal(SIGTERM, handler2);
	/* set input mode (non-canonical, no echo,...) */
	memcpy(&tio, &contio, sizeof tio);
	tio.c_lflag = ISIG;
	tio.c_cc[VTIME] = 0;   /* inter-character timer unused */
	tio.c_cc[VMIN] = 1;   /* blocking read until 1 char received */
	tcsetattr(STDIN_FILENO,TCSANOW,&tio);

	while (1) {
		fd_set fds;
		int retval;
		unsigned char c;
			
		FD_ZERO(&fds);
		FD_SET(STDIN_FILENO, &fds);
		FD_SET(portfd, &fds);

		retval = select(portfd + 1, &fds, NULL, NULL, NULL);
		if (FD_ISSET(portfd, &fds)) {
			if (1 == read(portfd, &c, 1))
				putchar(c);
		}
		if (FD_ISSET(STDIN_FILENO, &fds)) {
			if (1 == read(STDIN_FILENO, &c, 1))
				put_char(c);
		}
		fflush(NULL);
	}
}

/* wait for a character on the serial port */
unsigned char get_char(void)
{
	unsigned char c;
	assert(portfd >= 0);
	xread(portfd, &c, 1);
	return c;
}

/* wait for a character, or until a given timeout */
int get_char_timeout(int msecs)
{
	fd_set fds;
	struct timeval tv;

	assert(portfd >= 0);

	FD_ZERO(&fds);
	FD_SET(portfd, &fds);
	tv.tv_sec = 0;
	tv.tv_usec = msecs * 1000;

	select(portfd+1, &fds, NULL, NULL, &tv);
	if (FD_ISSET(portfd, &fds))
		return get_char();
	return -1;
}

/* send a character on the serial port */
void put_char(unsigned char c)
{
	assert(portfd >= 0);
	xwrite(portfd, &c, 1);
}

/* wait for a word on the serial port, LSB first */
unsigned get_word(void)
{
	unsigned w;
	
	w = get_char() & 0xff;
	w |= (get_char() & 0xff) << 8;
	w |= (get_char() & 0xff) << 16;
	w |= (get_char() & 0xff) << 24;
	return w;
}

/* send a word on the serial port, little-endian */
void put_word(unsigned w)
{
	put_char(w);
	put_char(w >> 8);
	put_char(w >> 16);
	put_char(w >> 24);
}

void put_block(const char *buf, unsigned size)
{
	assert(portfd >= 0);
	xawrite(portfd, buf, size);
}

/* ask the target to read a byte of memory */
unsigned char target_read_byte(unsigned addr)
{
	put_char('g');
	put_word(addr);
	return get_char();
}

/* tell the target to write a byte of memory */
void target_write_byte(unsigned addr, unsigned char data)
{
	put_char('s');
	put_word(addr);
	put_char(data);
}

/* ask the target to read a word of memory */
unsigned target_read_word(unsigned addr)
{
	put_char('r');
	put_word(addr);
	return get_word();
}

/* tell the target to write a word of memory */
void target_write_word(unsigned addr, unsigned data)
{
	put_char('w');
	put_word(addr);
	put_word(data);
}

/* tell the target to write a block of memory */
void target_write_block(unsigned addr, const char *buf,
			unsigned size, unsigned progress)
{
	assert(portfd >= 0);
	while (size > 0) {
		char checksum = 0;
		int step = min(size, SERIAL_BLOCKSIZE);
		
		put_char('W');
		put_word(addr);
		put_word(step);
		xawrite(portfd, buf, step);
		addr += step;
		size -= step;
		progress += step;
		while (step-- > 0) {
			checksum += *buf++;
		}
		if (checksum != (char)get_char()) {
			printf("\nSerial checksum error\n");
			exit(1);
		}
		printf("0x%08x\r", progress);
		fflush(NULL);
	}
}

