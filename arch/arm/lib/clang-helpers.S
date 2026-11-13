/*
 * arch/arm/lib/clang-helpers.S
 *
 * Custom assembly implementations of compiler-generated helper functions
 * to prevent compiler optimization recursion (e.g. bcmp -> memcmp -> bcmp).
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

	.text
	.align	5

/*
 * int bcmp(const void *s1, const void *s2, size_t n)
 */
ENTRY(bcmp)
	subs	r2, r2, #0
	moveq	r0, #0
	bxeq	lr
1:
	ldrb	r3, [r0], #1
	ldrb	ip, [r1], #1
	teq	r3, ip
	subne	r0, r3, ip
	bxne	lr
	subs	r2, r2, #1
	bne	1b
	mov	r0, #0
	bx	lr
ENDPROC(bcmp)

/*
 * char *stpcpy(char *dest, const char *src)
 */
ENTRY(stpcpy)
1:
	ldrb	r2, [r1], #1
	strb	r2, [r0], #1
	teq	r2, #0
	bne	1b
	sub	r0, r0, #1
	bx	lr
ENDPROC(stpcpy)

/*
 * size_t wcslen(const wchar_t *s)
 */
ENTRY(wcslen)
	mov	r1, r0
1:
	ldr	r2, [r1], #4
	teq	r2, #0
	bne	1b
	sub	r0, r1, r0
	sub	r0, r0, #4
	mov	r0, r0, lsr #2
	bx	lr
ENDPROC(wcslen)
