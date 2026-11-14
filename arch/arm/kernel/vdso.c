/*
 * Adapted from arm64 version.
 *
 * Copyright (C) 2012 ARM Limited
 * Copyright (C) 2015 Mentor Graphics Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Backported to 3.4: update_vsyscall() takes the timekeeper values as
 * arguments, and the kernel clocksource reads the physical counter
 * (CNTPCT) while user space can only read the virtual one (CNTVCT), so the
 * fast path is enabled only after checking that both counters agree
 * (CNTVOFF == 0) on every CPU.
 *
 * No cache maintenance after updating the data page: ARMv7 data caches
 * are PIPT/non-aliasing, the user mapping sees the same lines.
 */

#include <linux/clocksource.h>
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/notifier.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/time.h>
#include <asm/arch_timer.h>
#include <asm/page.h>
#include <asm/vdso.h>
#include <asm/vdso_datapage.h>

extern char vdso_start[], vdso_end[];

/* [vvar] data page + [vdso] text pages */
unsigned int vdso_total_pages __read_mostly;

static struct page **vdso_text_pagelist;
static unsigned int vdso_text_pages;

static union vdso_data_store vdso_data_store __page_aligned_data;
static struct vdso_data *vdso_data = &vdso_data_store.data;
static struct page *vdso_data_page;

/* CNTVCT equals the CNTPCT based kernel clocksource on all CPUs seen */
static bool cntvct_ok __read_mostly;

static inline u64 read_cntvct(void)
{
	u32 lo, hi;

	asm volatile("isb\n"
		     "mrrc p15, 1, %0, %1, c14" : "=r" (lo), "=r" (hi));
	return ((u64)hi << 32) | lo;
}

static inline u64 read_cntpct(void)
{
	u32 lo, hi;

	asm volatile("isb\n"
		     "mrrc p15, 0, %0, %1, c14" : "=r" (lo), "=r" (hi));
	return ((u64)hi << 32) | lo;
}

/*
 * vct1 <= pct + off <= vct2 holds for the real offset (vct = pct - off), so
 * CNTVOFF is 0 iff pct falls between the two virtual reads.
 */
static bool cntvoff_is_zero(void)
{
	u64 v1, p, v2;
	int i;

	for (i = 0; i < 3; i++) {
		v1 = read_cntvct();
		p = read_cntpct();
		v2 = read_cntvct();
		if (v1 <= p && p <= v2)
			return true;
	}
	return false;
}

static void vdso_check_cpu(void *arg)
{
	if (!cntvoff_is_zero())
		*(bool *)arg = false;
}

static void vdso_write_begin(struct vdso_data *vd)
{
	++vd->seq_count;
	smp_wmb();
}

static void vdso_write_end(struct vdso_data *vd)
{
	smp_wmb();
	++vd->seq_count;
}

/* runs on the CPU coming up, before it can schedule user tasks */
static int __cpuinit vdso_cpu_notify(struct notifier_block *nb,
				     unsigned long action, void *hcpu)
{
	if ((action & ~CPU_TASKS_FROZEN) == CPU_STARTING && cntvct_ok &&
	    !cntvoff_is_zero()) {
		pr_warn("vdso: CPU%lu: CNTVOFF != 0, clock_gettime() uses the syscall\n",
			(unsigned long)hcpu);
		/* update_vsyscall() drops the fast path on the next tick */
		cntvct_ok = false;
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata vdso_cpu_nb = {
	.notifier_call = vdso_cpu_notify,
};

static int __init vdso_init(void)
{
	unsigned int i;
	bool ok = true;

	if (memcmp(vdso_start, "\177ELF", 4)) {
		pr_err("vdso: image is not a valid ELF\n");
		return -EINVAL;
	}

	vdso_text_pages = (vdso_end - vdso_start) >> PAGE_SHIFT;
	vdso_text_pagelist = kcalloc(vdso_text_pages, sizeof(struct page *),
				     GFP_KERNEL);
	if (!vdso_text_pagelist)
		return -ENOMEM;

	for (i = 0; i < vdso_text_pages; i++)
		vdso_text_pagelist[i] = virt_to_page(vdso_start + i * PAGE_SIZE);

	vdso_data_page = virt_to_page(vdso_data);

	get_online_cpus();
	on_each_cpu(vdso_check_cpu, &ok, 1);
	if (ok)
		register_cpu_notifier(&vdso_cpu_nb);
	put_online_cpus();
	cntvct_ok = ok;

	/* published last: arm_install_vdso() does nothing until then */
	smp_wmb();
	vdso_total_pages = 1 + vdso_text_pages;

	pr_info("vdso: %u text page(s), %s\n", vdso_text_pages,
		ok ? "fast clock_gettime via CNTVCT" :
		     "CNTVOFF != 0, clock_gettime() uses the syscall");
	return 0;
}
arch_initcall(vdso_init);

/* called with mmap_sem held for write, addr has room for vdso_total_pages */
void arm_install_vdso(struct mm_struct *mm, unsigned long addr)
{
	int ret;

	mm->context.vdso = 0;

	if (!vdso_total_pages)
		return;

	ret = install_special_mapping(mm, addr, PAGE_SIZE,
				      VM_READ | VM_MAYREAD, &vdso_data_page);
	if (ret)
		return;

	addr += PAGE_SIZE;
	ret = install_special_mapping(mm, addr, vdso_text_pages << PAGE_SHIFT,
				      VM_READ | VM_EXEC |
				      VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC,
				      vdso_text_pagelist);
	if (!ret)
		mm->context.vdso = addr;
}

/* called by timekeeping with timekeeper.lock held for write */
void update_vsyscall(struct timespec *ts, struct timespec *wtm,
		     struct clocksource *c, u32 mult)
{
	/* the vDSO reads CNTVCT: only valid with the arch timer clocksource */
	bool fast = cntvct_ok && !strcmp(c->name, "arch_sys_counter");

	vdso_write_begin(vdso_data);

	vdso_data->tk_is_cntvct = fast;
	vdso_data->xtime_sec = ts->tv_sec;
	vdso_data->xtime_nsec = ts->tv_nsec;
	vdso_data->wtm_sec = wtm->tv_sec;
	vdso_data->wtm_nsec = wtm->tv_nsec;

	if (fast) {
		vdso_data->cs_cycle_last = c->cycle_last;
		vdso_data->cs_mask = c->mask;
		vdso_data->cs_mult = mult;
		vdso_data->cs_shift = c->shift;
	}

	vdso_write_end(vdso_data);
}

void update_vsyscall_tz(void)
{
	vdso_write_begin(vdso_data);
	vdso_data->tz_minuteswest = sys_tz.tz_minuteswest;
	vdso_data->tz_dsttime = sys_tz.tz_dsttime;
	vdso_write_end(vdso_data);
}
