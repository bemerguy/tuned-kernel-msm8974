#ifndef __ASM_ARM_WORD_AT_A_TIME_H
#define __ASM_ARM_WORD_AT_A_TIME_H

#ifndef __ARMEB__

/*
 * Little-endian word-at-a-time helpers for the dcache name hashing and
 * comparison in fs/namei.c and fs/dcache.c (the v3.4 x86 interface:
 * has_zero(), count_masked_bytes(), load_unaligned_zeropad()).
 * load_unaligned_zeropad() is the ARM one from mainline (v3.11).
 */
#include <linux/kernel.h>

/* Byte count of a mask of the form 000000 0000ff 00ffff ffffff */
static inline long count_masked_bytes(long mask)
{
	/* (000000 0000ff 00ffff ffffff) -> ( 1 1 2 3 ) */
	long a = (0x0ff0001 + mask) >> 23;
	/* Fix the 1 for 00 case */
	return a & mask;
}

/* Return the high bit set in the first byte that is a zero */
static inline unsigned long has_zero(unsigned long a)
{
	return ((a - REPEAT_BYTE(0x01)) & ~a) & REPEAT_BYTE(0x80);
}

#ifdef CONFIG_DCACHE_WORD_ACCESS

/*
 * Load an unaligned word from kernel space.
 *
 * In the (very unlikely) case of the word being a page-crosser
 * and the next page not being mapped, take the exception and
 * return zeroes in the non-existing part.
 */
static inline unsigned long load_unaligned_zeropad(const void *addr)
{
	unsigned long ret, offset, a = (unsigned long)addr;

	/* Load word from unaligned pointer addr */
	asm(
	"1:	ldr	%0, [%2]\n"
	"2:\n"
	"	.pushsection .fixup,\"ax\"\n"
	"	.align 2\n"
	"3:	and	%1, %2, #0x3\n"
	"	bic	%2, %2, #0x3\n"
	"	ldr	%0, [%2]\n"
	"	lsl	%1, %1, #0x3\n"
	"	lsr	%0, %0, %1\n"
	"	b	2b\n"
	"	.popsection\n"
	"	.pushsection __ex_table,\"a\"\n"
	"	.align	3\n"
	"	.long	1b, 3b\n"
	"	.popsection"
	: "=&r" (ret), "=&r" (offset), "+r" (a)
	: "Qo" (*(unsigned long *)addr));

	return ret;
}

#endif	/* DCACHE_WORD_ACCESS */

#else	/* __ARMEB__ */
#error "word-at-a-time: big-endian ARM is not supported"
#endif

#endif /* __ASM_ARM_WORD_AT_A_TIME_H */
