/*
 * init.S --	Startup code for loader.c, which runs in SRAM.
 * 
 * Copyright (C) 1999 Ben Williamson <benw@pobox.com>
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

#define SRAM_BASE	0x10000000	/* in bootstrap mode */
#define SRAM_SIZE	0x00000800	/* CL-PS7110 has 2kB */

.text
	.global _start
_start:
	mov	sp, #SRAM_BASE
	add	sp, sp, #SRAM_SIZE	/* stack in SRAM */
	mov     r0, #0
	ldr	r1, =__bss_start	/* clear bss */
1:	str	r0, [r1], #4
	cmp	r1, sp
	blo	1b

	bl	cmain			/* see loader.c */
2:	b	2b


	.global flush_v3
flush_v3:
	mov     r0, #0
	mcr     p15, 0, r0, c7, c7, 0	/* flush v3/v4 cache */
	mcr	p15, 0, r0, c5, c0, 0	/* flush v3 TLB */
	mov	pc, lr


	.global flush_v4
flush_v4:
	mov     r0, #0
	mcr     p15, 0, r0, c7, c7, 0	/* flush v3/v4 cache */
	mcr	p15, 0, r0, c8, c7, 0	/* flush v4 TLB */
	mov	pc, lr

