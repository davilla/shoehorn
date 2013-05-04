/*
 * shoehorn.c --	Host-side bootstrap loader for PS7111/EP7211 boards.
 * 
 * Copyright (c) 2000 Blue Mug, Inc.  All Rights Reserved.
 * Copyright (c) 1999 Ben Williamson <benw@pobox.com>
 *
 * This program loads a piece of code (see loader.c) into the target's
 * internal SRAM.  The loader then does our bidding to initialise and
 * detect hardware, load a kernel, ramdisk image, and boot.
 *
 * To add support for a new board type, add a new command-line option
 * that sets the hardware variable, and arrange for it to call
 * init_myboardname() instead of init_anvil().
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

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <getopt.h>
#include <errno.h>
#include <stdint.h>

#include "eth.h"
#include "ioregs.h"
#include "serial.h"
#include "util.h"
#include "cs8900.h"

#define _POSIX_SOURCE	1
/*
#define MAKE_VERSION(ver) "shoehorn v"## #ver ##" built " \
	__TIME__", "__DATE__ \
	" (CVS: $Id: shoehorn.c,v 1.10 2001/01/17 22:50:56 miket Exp $)";
static const char version[] 	= MAKE_VERSION(3.4);
*/

static const char version[] = "shoehorn v3.4-nyuu";

#define SRAM_SIZE	0x800		/* 2kB of SRAM for loader.bin */
#define START_CHAR	'<'
#define END_CHAR	'>'
#define MAX_FRAGS	100
#define DRAM_START	0xc0000000

#define MINORBITS	8
#define MKDEV(ma,mi)	(((ma) << MINORBITS) | (mi))
#define RAMDISK_MAJOR	1

/* architecture numbers are defined by the ARM kernel */
#define ARCH_NUMBER_EDB7211	50

#define PARAM_OFFSET	0x00000100	/* immediately after video RAM */
  #define PARAM_U1_OFFSET	0x00000100
  #define PARAM_U2_OFFSET	0x00000200
  #define PARAM_CMDLINE_OFFSET	0x00000600
  #define PARAM_END		0x00000A00
  #define PARAM_SIZE		(256 + 1024 + 1024)

#define KERNEL_OFFSET	0x00038000	/* beginning of kernel image */
#define PAGE		0x1000

#define INITRD_START	0xc0c00000

#define ETH_STEP	1024

static int ethernet = 0;
static int hardware = 0;
static int terminal = 0;

struct option options[] = {
	{ "anvil",	0, &hardware,	'a' },
	{ "edb7211",	0, &hardware,	'e' },
	{ "tracker",    0, &hardware,   't' },
	{ "phatbox",	0, &hardware,	'p' },
	{ "ethernet",	0, &ethernet,	1 },
	{ "initrd",	1, 0,		'i' },
	{ "kernel",	1, 0,		'k' },
	{ "loader",	1, 0,		'l' },
	{ "netif",	1, 0,		'n' },
	{ "port",	1, 0,		'p' },
	{ "terminal",	0, &terminal,	1 },
	{ "version",	0, 0,		'v' },
	{ 0,		0, 0,		0 }
};

/* this magic is documented in the GNU cpp info files; we use
   it here to let a Makefile or build script specify the path
   to loader.bin. */
#ifdef LOADERPATH
#define loaderpath(s) str(s)
#else
#define loaderpath(s)
#endif
#define str(s) #s

static int arch_number	= -1;
static char *initrd	= "initrd";
static char *kernel	= "Image";
static char *loader	= loaderpath(LOADERPATH) "loader.bin";
static char *netif	= "eth0";
static char *port	= "/dev/ttyS0";

char *progname		= "UNKNOWN";

char kargs[256];
unsigned char remotemac[6];

struct fragment {
	unsigned int	start;
	unsigned int	size;
} frag_list[MAX_FRAGS], *frag;

/*
 * Print usage instructions and exit
 */
void
usage_and_exit(void)
{
	printf("Usage: %s [options] [kernel command line]\n"
	       "Available options (defaults):\n"
	       "        --anvil\n"
	       "        --edb7211\n"
		   "        --tracker\n"
	       "        --phatbox\n"
	       "        --ethernet\n"
	       "        --initrd (%s)\n"
	       "        --kernel (%s)\n"
	       "        --loader (%s)\n"
	       "        --netif (%s)\n"
	       "        --port (%s)\n"
	       "        --terminal\n"
	       "        --version\n",
	       progname, initrd, kernel, loader, netif, port);
	exit(1);
}


/*
 * Parse the command line options
 */
static void
parse_command_line(int argc, char **argv)
{
	int c;
	
	while (1) {
		c = getopt_long_only(argc, argv, "iklnp", options, NULL);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 0:
			break;
		case 'i':
			initrd = optarg;
			break;
		case 'k':
			kernel = optarg;
			break;
		case 'l':
			loader = optarg;
			break;
		case 'n':
			netif = optarg;
			break;
		case 'p':
			port = optarg;
			break;
		case 'v':
			puts(version);
			exit(0);
		default:
			usage_and_exit();
		}
	}
	if (hardware == 0) {
		fprintf(stderr, "Need to specify hardware type (hint: --phatbox)\n");
		usage_and_exit();
	}
}

/*
 * concatenating what's left into kargs, but after root privs are dropped
 */
static void
build_kargs(int argc, char **argv)
{
	int remspace;
	int prependspace = 0;	/* 0 on first arg, else 1 */

	memset(kargs, 0, sizeof kargs);
	while (optind < argc) {
		remspace = sizeof(kargs) - strlen(kargs) - 1;
		/* add if it fits, exit if too large */
		if (remspace >= strlen(argv[optind]) + prependspace) {
			/* will fit */
			if (prependspace)	strcat(kargs, " ");
			strcat(kargs, argv[optind++]);
		} else {
			printf("Kernel options too long\n");
			exit(1);
		}
		prependspace = 1;
	}
	kargs[sizeof(kargs) - 1] = '\0';
}


void target_write_ethernet(unsigned addr, const char *buf,
			   unsigned size, unsigned progress)
{
	unsigned char frame [2048];

	while (size > 0) {
		unsigned char checksum = 0;
		unsigned char targetsum;
		unsigned step;
		int response, tries = 5;

		step = min(size, ETH_STEP);
		put_char('E');
		put_word(addr);
		put_word(step);

		/* ethernet frame format:
		   6-byte dest, 6-byte src, 2-byte dummy type and <=
		   1024-byte data data size must be an even number */

		memset(frame, 0, sizeof frame);
		memcpy(frame, remotemac, 6);
		*(unsigned short*)(frame + 12) = 0xabba;
		memcpy(frame + 14, buf, step);

		do {
			eth_write(frame, step + 14);
			targetsum = response = get_char_timeout(500);
		} while ((response < 0) && --tries);

		if (!tries) {
			printf("\nEthernet: no answer\n");
			exit(1);
		}

		addr += step;
		size -= step;
		progress += step;
		while (step-- > 0) {
			checksum += *buf++;
		}
		if (checksum != targetsum) {
			fprintf(stderr, "\nEthernet checksum error\n");
			exit(1);
		}
		printf("0x%08x\r", progress);
		fflush(NULL);
	}
}

void target_write(unsigned addr, const char *buf,
		  unsigned size, unsigned progress)
{
	/* XXX this is kind of nasty */
	if (ethernet)
		target_write_ethernet(addr, buf, size, progress);
	else
		target_write_block(addr, buf, size, progress);
}

/*
 * Write an object to the target, spanning multiple DRAM fragments
 * Returns final address
 */
unsigned int
target_write_fragmented(unsigned int addr, char *buf, unsigned int size)
{
	struct fragment *frag;
	unsigned int frag_end = 0;
	int progress = 0;
	
	for (frag = &frag_list[1]; frag->size != 0; frag++) {
		frag_end = frag->start + frag->size;
		if ((frag->start <= addr) && (addr < frag_end)) {
			break;
		}
	}
	while (size > 0) {
		int step = min(size, frag_end - addr);
		if (step <= 0) {
			printf("Insufficient DRAM space\n");
			exit(1);
		}
		target_write(addr, buf, step, progress);
		addr += step;
		buf += step;
		progress += step;
		size -= step;
		if (size > 0) {
			frag++;
			addr = frag->start;
			frag_end = frag->start + frag->size;
		}
	}
	if (addr == frag_end) {
		addr = frag[1].start;
	}
	return addr;
}


void
ping(void)
{
	unsigned char c;

	printf("Pinging loader\n");
	put_char('a');
	if ((c = get_char()) != '!') {
		printf("Got %02x\n", c);
		exit(1);
	}
}

/*
 * This definition of param_struct is copied from include/asm-arm/setup.h
 * in Linux 2.4.0-test6-rmk5.  Changes made after that kernel should
 * hopefully remain backwards compatible.
 *
 * Note that these will have to be changed to e.g. 'u32' to work on a 64-
 * bit development workstation.
 */
#define COMMAND_LINE_SIZE 1024

struct param_struct {
    union {
	struct {
	    uint32_t page_size;		/*  0 */
	    uint32_t nr_pages;		/*  4 */
	    uint32_t ramdisk_size;		/*  8 */
	    uint32_t flags;		/* 12 */
#define FLAG_READONLY	1
#define FLAG_RDLOAD	4
#define FLAG_RDPROMPT	8
	    uint32_t rootdev;		/* 16 */
	    uint32_t video_num_cols;	/* 20 */
	    uint32_t video_num_rows;	/* 24 */
	    uint32_t video_x;		/* 28 */
	    uint32_t video_y;		/* 32 */
	    uint32_t memc_control_reg;	/* 36 */
	    uint8_t sounddefault;		/* 40 */
	    uint8_t adfsdrives;		/* 41 */
	    uint8_t bytes_per_char_h;	/* 42 */
	    uint8_t bytes_per_char_v;	/* 43 */
	    uint32_t pages_in_bank[4];	/* 44 */
	    uint32_t pages_in_vram;	/* 60 */
	    uint32_t initrd_start;		/* 64 */
	    uint32_t initrd_size;		/* 68 */
	    uint32_t rd_start;		/* 72 */
	    uint32_t system_rev;		/* 76 */
	    uint32_t system_serial_low;	/* 80 */
	    uint32_t system_serial_high;	/* 84 */
	    uint32_t mem_fclk_21285;       /* 88 */ 
	} s;
	char unused[256];
    } u1;
    union {
	char paths[8][128];
	struct {
	    uint32_t magic;
	    char n[1024 - sizeof(uint32_t)];
	} s;
    } u2;
    char commandline[COMMAND_LINE_SIZE];
};


void
target_write_params(unsigned long initrd_start, unsigned long initrd_size)
{
	struct param_struct ps;

	/* sanity checking to keep updates from breaking anything */
	assert(PARAM_SIZE == PARAM_END - PARAM_OFFSET);
	assert(sizeof (struct param_struct) <= PARAM_SIZE);

	/* XXX these need to be generalized for different HW setups */
	/* XXX they are also endian specific */
	memset(&ps, 0, sizeof ps);
	/* printf("- page_size: %d\n", PAGE); */
	ps.u1.s.page_size = PAGE;
	printf("- nr_pages (all banks): %d\n", 4096);
	ps.u1.s.nr_pages = 4096;
	/* XXX */
	ps.u1.s.flags = FLAG_READONLY | FLAG_RDLOAD | FLAG_RDPROMPT;
	printf("- rootdev: (RAMDISK_MAJOR, 0)\n");
	ps.u1.s.rootdev = MKDEV(RAMDISK_MAJOR, 0);
	printf("- pages_in_bank[0]: %d\n", 2048);
	ps.u1.s.pages_in_bank[0] = 2048;
	printf("- pages_in_bank[1]: %d\n", 2048);
	ps.u1.s.pages_in_bank[1] = 2048;
	printf("- initrd_start: 0x%lx\n", initrd_start);
	ps.u1.s.initrd_start = initrd_start;
	printf("- initrd_size: 0x%lx\n", initrd_size);
	ps.u1.s.initrd_size = initrd_size;
	printf("- ramdisk_size: 0x%lx\n", initrd_size);
	ps.u1.s.ramdisk_size = initrd_size;

	/* XXX these writes overlap */
	print_size(DRAM_START + PARAM_OFFSET, PARAM_SIZE);
	target_write_fragmented(DRAM_START + PARAM_OFFSET,
		(char*) &ps, PARAM_SIZE);
	target_write_fragmented(DRAM_START + PARAM_CMDLINE_OFFSET,
		kargs, sizeof(kargs));
}

	
void
init_anvil(void)
{
	printf("- flushing cache/TLB\n");
	put_char('4');

	printf("- 73MHz core clock\n");
	/* IO_SYSCON3 = (IO_SYSCON3 & ~CLKCTL) | CLKCTL_73 */
	target_write_word(IO(SYSCON3),
		(target_read_word(IO(SYSCON3)) & ~CLKCTL) | CLKCTL_73);
	
	printf("- 32kHz DRAM refresh\n");
	/* IO_DRFPR = 0x83 */
	target_write_word(IO(DRFPR), 0x83);

	printf("- enabling UART2\n");
	/* IO_PDDR = 0x10 */
	target_write_byte(IO(PDDR), 0x10); /* enable AuxPwrGate transceiver */
	/* IO_SYSCON2 |= UART2EN */
	target_write_word(IO(SYSCON2),
		target_read_word(IO(SYSCON2)) | UART2EN);

	printf("Switching to 115200 baud\n");
	/* IO_UBRLCR1 = IO_UBRLCR1 & ~BRDIV | BR_115200 */
	target_write_word(IO(UBRLCR1),
		(target_read_word(IO(UBRLCR1)) & ~BRDIV) | BR_115200);
	serial_baud(B115200);
}


void
post_anvil(void)
{
	printf("Switching back to 9600 baud\n");
	/* IO_UBRLCR1 = IO_UBRLCR1 & ~BRDIV | BR9600 */
	target_write_word(IO(UBRLCR1),
		(target_read_word(IO(UBRLCR1)) & ~BRDIV) | BR_9600);
	serial_baud(B9600);
}

	
void
init_edb7211(void)
{
	printf("- flushing cache/TLB\n");
	put_char('4');

	printf("- 73MHz core clock\n");
	/* IO_SYSCON3 = (IO_SYSCON3 & ~CLKCTL) | CLKCTL_73 */
	target_write_word(IO(SYSCON3),
		(target_read_word(IO(SYSCON3)) & ~CLKCTL) | CLKCTL_73);
	
	printf("- 64kHz DRAM refresh\n");
	/* IO_DRFPR = 0x81 */
	target_write_word(IO(DRFPR), 0x81);

	printf("- enabling UART2\n");
	/* IO_PDDR = 0x10 */
	/* XXX is this right for EDB7211? */
	target_write_byte(IO(PDDR), 0x10); /* enable AuxPwrGate transceiver */
	/* IO_SYSCON2 |= UART2EN */
	target_write_word(IO(SYSCON2),
		target_read_word(IO(SYSCON2)) | UART2EN);

	printf("- Activate LED flasher\n");
	/* IO_LEDFLSH = 0x40 */
	target_write_byte(IO(LEDFLSH), 0x40);

	printf("- Setting up flash at CS0 and CS1, 32 Bit, 3 Waitstates\n");
	/* IO_MEMCFG1 = (IO_MEMCFG1 & 0xffff0000) | 0x00001414 */
	target_write_word(IO(MEMCFG1),
		(target_read_word(IO(MEMCFG1)) & 0xffff0000) | 0x00001414);

	printf("- Setting up CS8900 (Ethernet) at CS2, 32 Bit, 5 Waitstates\n");
	/* IO_MEMCFG1 = (IO_MEMCFG1 & 0xff00ffff) | 0x000c0000 */
	target_write_word(IO(MEMCFG1),
		(target_read_word(IO(MEMCFG1)) & 0xff00ffff) | 0x000c0000);

	printf("- Setting up Keyboard at CS3, 8 Bit, 3 Waitstates\n");
	/* IO_MEMCFG1 = (IO_MEMCFG1 & 0x00ffffff) | 0x16000000 */
	target_write_word(IO(MEMCFG1),
		(target_read_word(IO(MEMCFG1)) & 0x00ffffff) | 0x16000000);

	printf("Switching to 115200 baud\n");
	/* IO_UBRLCR1 = IO_UBRLCR1 & ~BRDIV | BR_115200 */
	target_write_word(IO(UBRLCR1),
		(target_read_word(IO(UBRLCR1)) & ~BRDIV) | BR_115200);
	serial_baud(B115200);
}


void
post_edb7211(void)
{
	printf("Switching back to 9600 baud\n");
	/* IO_UBRLCR1 = IO_UBRLCR1 & ~BRDIV | BR9600 */
	target_write_word(IO(UBRLCR1),
		(target_read_word(IO(UBRLCR1)) & ~BRDIV) | BR_9600);
	serial_baud(B9600);
}


void
init_tracker(void)
{
	printf("- flushing cache/TLB\n");
	put_char('4');

	printf("- 73MHz core clock\n");
	/* IO_SYSCON3 = (IO_SYSCON3 & ~CLKCTL) | CLKCTL_73 */
	target_write_word(IO(SYSCON3),
		(target_read_word(IO(SYSCON3)) & ~CLKCTL) | CLKCTL_73);

#define SDCONF 0x2300
#define SDRFPR 0x2340
	printf("- SDRAM 64Mbit, CAS=2 W=16\n");
	/* IO_DRFPR = 0x81 */
	target_write_word(IO(SDCONF), 0x522);

	//printf("- 64kHz DRAM refresh\n");	//Normally done but not effective
	///* IO_DRFPR = 0x81 */				//as it can be
	//target_write_word(IO(), 0x81);

	printf("- Activate LED flasher\n");
	/* IO_LEDFLSH = 0x40 */
	target_write_byte(IO(LEDFLSH), 0x40);

	//printf("- Setting up flash at CS0, 32 Bit, 3 Waitstates\n");
	//target_write_word(IO(MEMCFG1),
	//	(target_read_word(IO(MEMCFG1)) & 0xffff0000) | 0x00000001);

	printf("Switching to 115200 baud\n");
	/* IO_UBRLCR1 = IO_UBRLCR1 & ~BRDIV | BR_115200 */
	target_write_word(IO(UBRLCR1),
		(target_read_word(IO(UBRLCR1)) & ~BRDIV) | BR_115200);
	serial_baud(B115200);
}


void
post_tracker(void)
{
	printf("Switching back to 9600 baud\n");
	/* IO_UBRLCR1 = IO_UBRLCR1 & ~BRDIV | BR9600 */
	target_write_word(IO(UBRLCR1),
		(target_read_word(IO(UBRLCR1)) & ~BRDIV) | BR_9600);
	serial_baud(B9600);
}

void
init_phatbox(void)
{
	
  /* taken from the PhatBox bootloader ROM code:
   *    register_set_table     ; DATA XREF: offset_table:register_table_offset
   *                           ; format:
   *                           ; register, mask, value
   *		               ; *register= (*register & !mask) | value
   *	   DCD 0x80000100,0xFFFFFFFF,0x40100	 ; SYSCON1=UART1EN | EXCKEN
   *       DCD 0x80001100,0xFFFFFFFF,0x100       ; SYSCON2 = UART2EN
   *       DCD 0x80000180,0xFFFFFFFC,0x80        ; MEMCFG1 = CLKENB (for flash rom)
   *       DCD 0x800001C0,0xFFFFFFFF,0xBD00      ; MEMCFG2 - cs5 = CLKENB| SQAENB | 1 wait state | 16 bits
   *       DCD 0x80000040,0xFF00FFFF,0xFF000000  ; PADDR
   *       DCD 0x80000000,0xFF00FFFF,   0        ; PADR
   *       DCD 0x800000C0,0xFF,   0              ; PEDDR
   *       DCD 0x80000080,0xFF,   0              ; PEDR
   *       DCD    0,   0,   0  
   */

  printf("- flushing cache/TLB\n");
  put_char('4');
  
  
  printf("SYSCON1: Enabling UART1 and EXCKEN\n");
  target_write_word(IO(SYSCON1),0x40100);
  
  printf("MEMCFG1: Enabling CLKENB for flash rom (MEMCFG1)\n");
  target_write_word(IO(MEMCFG1),
		    (target_read_word(IO(MEMCFG1)) & (~3)) | 0x80);
  
  printf("MEMCFG2: Setting SDRAM parms -- CLKENB, SQAENB, 1 wait state, 16 bits\n");
  target_write_word(IO(MEMCFG2), 0xBD00);
  
  printf("PADDR: Setting Port A Direction register...\n");
  target_write_word(IO(PADDR), 
		    (target_read_word(IO(PADDR)) & 0xFF00FFFF) | 0xFF000000);
  
  printf("PADR: Setting Port A Data register...\n");
  target_write_word(IO(PADR), 
		    (target_read_word(IO(PADR)) & 0xFF00FFFF) | 0);
  
  printf("PEDDR: Setting Port E Direction register...\n");
  target_write_word(IO(PEDDR), 
		    (target_read_word(IO(PEDDR)) & 0xFF) | 0);
  
  printf("PEDR: Setting Port E Data register...\n");
  target_write_word(IO(PEDR), 
			  (target_read_word(IO(PEDR)) & 0xFF) | 0);
  
	
	printf("Switching to 115200 baud\n");
	/* IO_UBRLCR1 = IO_UBRLCR1 & ~BRDIV | BR_115200 */
	target_write_word(IO(UBRLCR1),
		(target_read_word(IO(UBRLCR1)) & ~BRDIV) | BR_115200);
	serial_baud(B115200);
}


void
post_phatbox(void)
{
	printf("Switching back to 9600 baud\n");
	/* IO_UBRLCR1 = IO_UBRLCR1 & ~BRDIV | BR9600 */
	target_write_word(IO(UBRLCR1),
		(target_read_word(IO(UBRLCR1)) & ~BRDIV) | BR_9600);
	serial_baud(B9600);
}


void
init_8051(void)
{
	unsigned char c,d,e,f;

	c=d=e=f=0;

	put_char('i');
	while (((c=get_char()) != 0xff) || (d != 0x00) || (e != 0xff) || (f != 0x00)) {
		printf("Got %02x\n", c);
		f=e;
		e=d;
		d=c;
	}
}

void
detect_dram(void)
{
	unsigned int start, size, total_size;
	
	put_char('d');
	printf("- %d bits wide\n", get_char());
	frag_list[0].start = 0;	/* item 0 is a dummy, list starts at 1 */
	frag_list[0].size = 0;
	total_size = 0;
	frag = frag_list;
	size = get_word();
	while (1) {
		start = get_word();
		if (start == 0) {
			break;
		}
		if (start == frag->start + frag->size) {
			frag->size += size;
		} else {
			frag++;
			if (frag >= &frag_list[MAX_FRAGS-1]) {
				printf("Too many DRAM fragments\n");
				exit(1);
			}
			frag->start = start;
			frag->size = size;
		}
		total_size += size;
	}
	frag++;
	frag->start = frag->size = 0;
	for (frag = &frag_list[1]; frag->size != 0; frag++)
		print_size(frag->start, frag->size);
	printf("Total DRAM: %dkB\n", total_size / 1024);
}


void perror_usage_exit(const char *s)
{
	fprintf(stderr, "%s: ", progname);
	fprintf(stderr, "%s: %s\n", s, strerror(errno));
	usage_and_exit();
}


/*
 * Send loader.bin to the target
 * Initialise some target registers, for Anvil hardware
 * Detect what DRAM is present, building a list of fragments
 * Load the kernel image and initrd into available fragments
 * Write a kernel parameter block with command-line arguments
 * Jump to the kernel entry point
 */
int
main(int argc, char **argv)
{
	unsigned char *initrd_buf, *kernel_buf, *loader_buf;
	unsigned kernel_size, initrd_size, loader_size;
	unsigned long kernel_end, initrd_start = INITRD_START, size;
	int i;
	uid_t ruid, euid, suid;
	int getresuid(uid_t *, uid_t *, uid_t *);	/* linux only????? */
	
	progname = strrchr(argv[0], '/');
	if (progname == NULL) {
		progname = argv[0];
	} else {
		progname++;
	}
	parse_command_line(argc, argv);

	/* make output to stdout immediately visible */
	setbuf(stdout, NULL);

	/* initialize Ethernet */
	if (ethernet) {
		printf("Initializing local network interface\n");
		eth_open(netif);
	}

	/* drop root privs if setuid root */
	if ( (geteuid() == 0) && (getuid() != 0) ) {
		printf("Dropping root privileges\n");
		if (setuid(getuid()) != 0) {
			fprintf(stderr, "%s: error in droping root-privs\n",
								progname);
			exit(EXIT_FAILURE);
		}
		/* FIXME: make it portable to systems w/o saved uid */
		/* check if root privs are really dropped */
		if (getresuid(&ruid, &euid, &suid) != 0) {
			fprintf(stderr, "%s: could not check if root-privs "
						"were dropped\n", progname);
			exit(EXIT_FAILURE);
		}
		if ( (ruid == 0) || (euid == 0) || (suid == 0) ) {
			fprintf(stderr, "%s: not all root-privs were dropped\n"
					"\truid: %d, euid: %d, suid: %d\n",
				       	progname, ruid, euid, suid);
			exit(EXIT_FAILURE);
		}
	}

	/* fill in kargs *after* dropping privileges */
	build_kargs(argc, argv);

	/* slurp files into buffers */
	loader_size = SRAM_SIZE;  /* must allocate at least SRAM_SIZE bytes */
	read_file(loader, &loader_buf, &loader_size);
	kernel_size = 0;
	read_file(kernel, &kernel_buf, &kernel_size);
	initrd_size = 0;
	read_file(initrd, &initrd_buf, &initrd_size);

	/* make sure loader isn't too big */
	if (loader_size > SRAM_SIZE) {
		fprintf(stderr, "%s: loader too large (limit %d bytes)\n",
			progname, SRAM_SIZE);
		exit(1);
	}
	if (loader_size > SRAM_SIZE - 0x100)
		fprintf(stderr, "%s: warning: loader stack might clobber code\n",
				progname);

	/* open serial port and start talking to hardware */
	serial_open(port);
	printf("Waiting for target - press Wakeup now. (ie turn it on!)\n");
	if (get_char() != START_CHAR) {
		printf("Expected start character '%c'\n", START_CHAR);
		exit(1);
	}
	printf("Writing SRAM loader...\n");
	put_block(loader_buf, SRAM_SIZE);
	if (get_char() != END_CHAR) {
		printf("Expected end character '%c'\n", END_CHAR);
		exit(1);
	}
	ping();
	
	switch(hardware) {
	case 'a':
		/* Anvil hardware doesn't appear to have an arch. number;
		   you'll need to use a local, temporary one here to get
		   things working. */
		printf("Initialising Anvil hardware:\n");
		init_anvil();
		break;
	case 'e':
		arch_number = ARCH_NUMBER_EDB7211;
		printf("Initialising EDB7211 hardware:\n");
		init_edb7211();
		break;
	case 'p':
                arch_number = 170;  // ARCH_CLEP7312
	        printf("Initializing PhatBox (CLEP7312) hardware:\n");
	        init_phatbox();
	        break;
	case 't':
            arch_number = 0x5b;  // Cirrus Logic 7212/7312
	        printf("Initializing tracker (CLEP7312) hardware:\n");
	        init_tracker();
	        break;
	default:
		printf("Internal error - invalid hardware value\n");
		exit(1);
	}
	assert(arch_number > 0);
	ping();

	if (ethernet) {
		unsigned	allff;
		unsigned	allzero;
		unsigned short	status;

		printf("Initializing remote Ethernet\n");
		put_char('e');
		status = get_char();
		status += get_char() << 8;
		printf("cs8900 status: %04x (", status);
		if (status & PP_SelfSTAT_EEPROM) {
			printf("EEPROM present:");
			if (status & PP_SelfSTAT_EEPROM_OK)
				printf(" OK, ");
			printf(" size = ");
			if (status & PP_SelfSTAT_EEsize) {
				printf("64");
			} else {
				printf("128/256");
			}
			printf(" words");
		}
		printf(")\n");
		if (get_char() != '+') {
			fprintf(stderr, "%s: Ethernet initialization error\n",
					progname);
			exit(1);
		}
		printf("MAC address of target is ");
		allff = 1;
		allzero = 1;
		for (i=0; i<6; i++) {
			remotemac[i] = get_char();
			if (remotemac[i] != 0xff) {
				allff = 0;
			}
		       	if (remotemac[i] != 0x00) {
				allzero = 0;
			}
			putchar('.');
		}
		putchar(' ');
		allff |= allzero;
		if (allff) {
			printf("uninitialized; setting.\n");
			put_char('M');
			/* set 12:34:56:78:9a:bc as MAC address */
			for (i=0; i<6; i++) {
				put_char(0x12 + i * 0x22);
			}
			printf("Now MAC address of device is ");
		}
		for (i=0; i<6; i++) {
			remotemac[i] = allff ? get_char() : remotemac[i];
			printf("%02X%c", remotemac[i], i==5 ? '\n' : ':');
		}
	}
	if (hardware == 'p'){
		printf("Initializing 8051\n");
		init_8051();
	}
	printf("Detecting DRAM\n");
	detect_dram();
	
	printf("Loading %s:\n", kernel);
	print_size(DRAM_START + KERNEL_OFFSET, kernel_size);
	kernel_end = target_write_fragmented(DRAM_START + KERNEL_OFFSET,
		kernel_buf, kernel_size);
	free(kernel_buf);

	/* Find a start address for initrd.  We put it
	   at the highest possible page-aligned address. */
	size = (initrd_size + PAGE - 1) & ~(PAGE - 1);
	for (frag = &frag_list[1]; frag->size != 0; frag++);
	frag--;
	while ((size > 0) && (frag > frag_list)) {
		int step = min(size, frag->size);
		initrd_start = frag->start + frag->size - step;
		size -= step;
		frag--;
	}
	/* XXX miket: trying this out */
	printf("initrd_start is %lx\n", initrd_start);
	initrd_start = INITRD_START;
	printf("Moving initrd_start to %lx\n", initrd_start);
	/* end XXX area */
	if (initrd_start < kernel_end) {
		printf("Not enough space for initrd\n");
		exit(1);
	}

	printf("Loading %s:\n", initrd);
	print_size(initrd_start, initrd_size);
	target_write_fragmented(initrd_start, initrd_buf, initrd_size);
	free(initrd_buf);
	
	printf("Writing parameter area\n");
	target_write_params(initrd_start, initrd_size);
	
	switch(hardware) {
	case 'a':
		post_anvil();
		break;
	case 'e':
		post_edb7211();
		break;
	case 'p':
		post_phatbox();
		break;	
	case 't':
		post_tracker();
		break;
	default:
		fprintf(stderr, "%s: Internal error - invalid hardware value\n",
				progname);
		exit(1);
	}
	ping();

	if (ethernet)
		eth_close();
	
	printf("Starting kernel\n");
	put_char('c');
	put_word(DRAM_START + KERNEL_OFFSET);
	put_word(0);	/* sanity check */
	put_word(arch_number);
	put_word(0);
	put_word(0);

	/* I've found this bit useful for debugging: If the kernel doesn't
	   seem to boot, run "shoehorn --terminal" and plug head-armv.S
	   and init/main.c full with things like "IO_UARTDR1 = '!';" or
	   similar.  Then you'll find out how far the setup code gets. */
	if (terminal)
		serial_terminal();

	serial_close();
	return 0;
}

