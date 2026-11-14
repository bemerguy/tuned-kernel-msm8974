/*
 * Adapted from arm64 version.
 *
 * Copyright (C) 2012 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_VDSO_DATAPAGE_H
#define __ASM_VDSO_DATAPAGE_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

#include <asm/page.h>

/*
 * Written by update_vsyscall() with the 3.4 timekeeper values, read by the
 * vDSO. The time is xtime + cyc2ns(counter - cycle_last), exactly what
 * getnstimeofday()/ktime_get_ts() compute in the kernel.
 */
struct vdso_data {
	u32 seq_count;		/* sequence count - odd during updates */
	u16 tk_is_cntvct;	/* counter readable from user: else syscall */
	u16 cs_shift;		/* clocksource shift */
	s32 xtime_sec;		/* CLOCK_REALTIME */
	u32 xtime_nsec;
	s32 wtm_sec;		/* wall to monotonic offset */
	u32 wtm_nsec;
	u32 cs_mult;		/* NTP adjusted clocksource multiplier */
	u32 tz_minuteswest;	/* timezone info for gettimeofday(2) */
	u64 cs_cycle_last;	/* counter value at the last update */
	u64 cs_mask;		/* clocksource mask */
	u32 tz_dsttime;
};

union vdso_data_store {
	struct vdso_data data;
	u8 page[PAGE_SIZE];
};

#endif /* !__ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* __ASM_VDSO_DATAPAGE_H */
