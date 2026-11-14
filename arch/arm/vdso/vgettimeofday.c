/*
 * Copyright 2015 Mentor Graphics Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Backported to the 3.4 timekeeper: the data page holds xtime (whole
 * nanoseconds) and the NTP adjusted mult, which is what the kernel itself
 * uses in getnstimeofday() and ktime_get_ts().
 */

#include <linux/compiler.h>
#include <linux/time.h>
#include <linux/types.h>
#include <asm/unistd.h>
#include <asm/vdso_datapage.h>

/*
 * The data page sits right below the vDSO text (see vdso.lds.S). Not const:
 * the kernel rewrites it, and a const object would let the compiler move
 * the loads out of the seq_count protected section.
 */
extern struct vdso_data _vdso_data __attribute__((visibility("hidden")));

/* plain ARMv7 barriers: the kernel's may call out (CONFIG_ARCH_HAS_BARRIERS) */
#define vdso_rmb()	__asm__ __volatile__("dmb ish" : : : "memory")

static notrace u32 vdso_read_begin(const struct vdso_data *vd)
{
	u32 seq;

	for (;;) {
		seq = ACCESS_ONCE(vd->seq_count);
		if (!(seq & 1))
			break;
		barrier();
	}
	vdso_rmb();
	return seq;
}

static notrace int vdso_read_retry(const struct vdso_data *vd, u32 start)
{
	vdso_rmb();
	return ACCESS_ONCE(vd->seq_count) != start;
}

static notrace long clock_gettime_fallback(clockid_t _clkid,
					   struct timespec *_ts)
{
	register struct timespec *ts asm("r1") = _ts;
	register clockid_t clkid asm("r0") = _clkid;
	register long ret asm("r0");
	register long nr asm("r7") = __NR_clock_gettime;

	asm volatile("	swi #0\n"
		     : "=r" (ret)
		     : "r" (clkid), "r" (ts), "r" (nr)
		     : "memory");

	return ret;
}

static notrace long gettimeofday_fallback(struct timeval *_tv,
					  struct timezone *_tz)
{
	register struct timezone *tz asm("r1") = _tz;
	register struct timeval *tv asm("r0") = _tv;
	register long ret asm("r0");
	register long nr asm("r7") = __NR_gettimeofday;

	asm volatile("	swi #0\n"
		     : "=r" (ret)
		     : "r" (tv), "r" (tz), "r" (nr)
		     : "memory");

	return ret;
}

static notrace u64 read_cntvct(void)
{
	u32 lo, hi;

	/* the ISB keeps the read from being hoisted above the seq read */
	asm volatile("isb\n"
		     "mrrc p15, 1, %0, %1, c14" : "=r" (lo), "=r" (hi));
	return ((u64)hi << 32) | lo;
}

static notrace u64 get_ns(const struct vdso_data *vd)
{
	u64 delta = (read_cntvct() - vd->cs_cycle_last) & vd->cs_mask;

	/* clocksource_cyc2ns() */
	return (delta * vd->cs_mult) >> vd->cs_shift;
}

/* timespec_add_ns(): subtract instead of dividing (no libgcc in here) */
static notrace void set_ts(struct timespec *ts, long sec, u64 nsec)
{
	while (nsec >= NSEC_PER_SEC) {
		/* keep the compiler from turning this into a 64-bit division */
		asm("" : "+r" (nsec));
		nsec -= NSEC_PER_SEC;
		sec++;
	}
	ts->tv_sec = sec;
	ts->tv_nsec = (long)nsec;
}

static notrace int do_realtime(const struct vdso_data *vd, struct timespec *ts)
{
	u32 seq;
	long sec;
	u64 nsec;

	do {
		seq = vdso_read_begin(vd);
		if (!vd->tk_is_cntvct)
			return -1;
		sec = vd->xtime_sec;
		nsec = vd->xtime_nsec + get_ns(vd);
	} while (vdso_read_retry(vd, seq));

	set_ts(ts, sec, nsec);
	return 0;
}

static notrace int do_monotonic(const struct vdso_data *vd, struct timespec *ts)
{
	u32 seq;
	long sec;
	u64 nsec;

	do {
		seq = vdso_read_begin(vd);
		if (!vd->tk_is_cntvct)
			return -1;
		sec = vd->xtime_sec + vd->wtm_sec;
		nsec = (u64)vd->xtime_nsec + vd->wtm_nsec + get_ns(vd);
	} while (vdso_read_retry(vd, seq));

	set_ts(ts, sec, nsec);
	return 0;
}

static notrace void do_realtime_coarse(const struct vdso_data *vd,
				       struct timespec *ts)
{
	u32 seq;
	long sec, nsec;

	do {
		seq = vdso_read_begin(vd);
		sec = vd->xtime_sec;
		nsec = vd->xtime_nsec;
	} while (vdso_read_retry(vd, seq));

	ts->tv_sec = sec;
	ts->tv_nsec = nsec;
}

static notrace void do_monotonic_coarse(const struct vdso_data *vd,
					struct timespec *ts)
{
	u32 seq;
	long sec;
	u64 nsec;

	do {
		seq = vdso_read_begin(vd);
		sec = vd->xtime_sec + vd->wtm_sec;
		nsec = (u64)vd->xtime_nsec + vd->wtm_nsec;
	} while (vdso_read_retry(vd, seq));

	set_ts(ts, sec, nsec);
}

notrace int __vdso_clock_gettime(clockid_t clkid, struct timespec *ts)
{
	const struct vdso_data *vd = &_vdso_data;

	switch (clkid) {
	case CLOCK_REALTIME_COARSE:
		do_realtime_coarse(vd, ts);
		return 0;
	case CLOCK_MONOTONIC_COARSE:
		do_monotonic_coarse(vd, ts);
		return 0;
	case CLOCK_REALTIME:
		if (do_realtime(vd, ts))
			break;
		return 0;
	case CLOCK_MONOTONIC:
		if (do_monotonic(vd, ts))
			break;
		return 0;
	}

	return clock_gettime_fallback(clkid, ts);
}

notrace int __vdso_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	const struct vdso_data *vd = &_vdso_data;
	struct timespec ts;

	if (tv) {
		if (do_realtime(vd, &ts))
			return gettimeofday_fallback(tv, tz);
		tv->tv_sec = ts.tv_sec;
		tv->tv_usec = (u32)ts.tv_nsec / 1000;
	}
	if (tz) {
		tz->tz_minuteswest = vd->tz_minuteswest;
		tz->tz_dsttime = vd->tz_dsttime;
	}

	return 0;
}
