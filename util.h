/*
 * Copyright (c) 2000 Blue Mug, Inc.  All Rights Reserved.
 */

#ifndef _SHOEHORN_UTIL_H
#define _SHOEHORN_UTIL_H

#define min(a,b)	((a)<(b) ? (a) : (b))

extern void perror_exit(const char *errstr);
extern void print_size(unsigned start, unsigned size);
extern void read_file(const char *filename, unsigned char **buf,
		      unsigned *size);

extern void *xmalloc(size_t size);

extern void xclose(int fd);
extern ssize_t xread(int fd, void *buf, size_t count);
extern ssize_t xaread(int fd, void *buf, size_t count);
extern ssize_t xwrite(int fd, const void *buf, size_t count);
extern ssize_t xawrite(int fd, const void *buf, size_t count);

#endif /* _SHOEHORN_UTIL_H */
