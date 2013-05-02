/*
 * Copyright (c) 2000 Blue Mug, Inc.  All Rights Reserved.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "util.h"


extern char *progname;


void perror_exit(const char *errstr)
{
	fprintf(stderr, "%s: ", progname);
	fprintf(stderr, "%s: %s\n", errstr,strerror(errno));
	exit(1);
}

void print_size(unsigned start, size_t size)
{
	if (!size)
		printf("- start: 0x%08x size: 0x%08x\n", start, size);
	else
		printf("- start: 0x%08x size: 0x%08x last: 0x%08x\n",
			start, size, start + size - 1);
}

void read_file(const char *filename, unsigned char **buf, unsigned *size)
{
	FILE *f;
	unsigned rdsize;
	unsigned bufsize;

	f = fopen(filename, "r");
	if (!f)
		perror_exit(filename);
	fseek(f, 0L, SEEK_END);
	bufsize = rdsize = ftell(f);
	if (*size > rdsize) {	/* min buffer size requested */
		bufsize = *size;
	}
	/* round up to even # of bytes for eth loader */
	if (bufsize & 1)
		bufsize += 1;
	fseek(f, 0L, SEEK_SET);
	*buf = xmalloc(bufsize);
	if (rdsize != fread(*buf, 1, rdsize, f)) {
		printf("%s: couldn't read whole file\n", filename);
		exit(1);
	}
	fclose(f);
	printf("%s: %d bytes", filename, rdsize);
	if (rdsize != bufsize) {
		printf(" (%d bytes buffer)", bufsize);
	}
	printf("\n");
	*size = rdsize;
	if (*size & 1)
		*size += 1;
}

void *xmalloc(size_t size)
{
	void *value = malloc(size);
	if (!value) {
		fprintf(stderr, "virtual memory exhausted\n");
		exit(1);
	}
	return value;
}

void xclose(int fd)
{
	if (close(fd) < 0)
		perror_exit("close");
}

/* read or die */
ssize_t xread(int fd, void *buf, size_t count)
{
	ssize_t retval;
	do {
		retval = read(fd, buf, count);
	} while ((retval < 0) && (errno == EINTR));
	if (retval < 0)
		perror_exit("read");
	return retval;
}

/* read entire buffer or die */
ssize_t xaread(int fd, void *buf, size_t count)
{
	int nread, remain;
	for (remain = count; remain; remain -= nread)
		buf += (nread = xread(fd, buf, remain));
	return count;
}

/* write or die */
ssize_t xwrite(int fd, const void *buf, size_t count)
{
	ssize_t retval;
	do {
		retval = write(fd, buf, count);
	} while ((retval < 0) && (errno == EINTR));
	if (retval < 0)
		perror_exit("write");
	return retval;
}

/* write entire buffer or die */
ssize_t xawrite(int fd, const void *buf, size_t count)
{
	int nwritten, remain;
	for (remain = count; remain; remain -= nwritten)
		buf += (nwritten = xwrite(fd, buf, remain));
	return count;
}

