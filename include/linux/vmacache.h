#ifndef __LINUX_VMACACHE_H
#define __LINUX_VMACACHE_H

#include <linux/sched.h>
#include <linux/mm.h>

/*
 * Per-thread VMA cache (v3.15 615d6e8756c8 "mm: per-thread vma caching"),
 * in the v4.19 form with a 64-bit sequence number and no
 * vmacache_flush_all() (7a9cdebdcc17, CVE-2018-17182).
 *
 * Hash based on the page number. Provides a good hit rate for workloads
 * with good locality and those with random accesses as well.
 */
#define VMACACHE_HASH(addr) ((addr >> PAGE_SHIFT) & VMACACHE_MASK)

static inline void vmacache_flush(struct task_struct *tsk)
{
	memset(tsk->vmacache, 0, sizeof(tsk->vmacache));
}

extern void vmacache_update(unsigned long addr, struct vm_area_struct *newvma);
extern struct vm_area_struct *vmacache_find(struct mm_struct *mm,
					    unsigned long addr);

/* called with mmap_sem held for write whenever a vma is unlinked */
static inline void vmacache_invalidate(struct mm_struct *mm)
{
	mm->vmacache_seqnum++;
}

#endif /* __LINUX_VMACACHE_H */
