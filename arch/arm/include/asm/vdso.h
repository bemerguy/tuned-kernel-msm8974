#ifndef __ASM_VDSO_H
#define __ASM_VDSO_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

struct mm_struct;

#ifdef CONFIG_VDSO

/* number of pages to map after the sigpage: [vvar] + [vdso] text */
extern unsigned int vdso_total_pages;

void arm_install_vdso(struct mm_struct *mm, unsigned long addr);

#else /* CONFIG_VDSO */

#define vdso_total_pages 0

static inline void arm_install_vdso(struct mm_struct *mm, unsigned long addr)
{
}

#endif /* CONFIG_VDSO */

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* __ASM_VDSO_H */
