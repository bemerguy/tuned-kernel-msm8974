#ifndef __ASM_AUXVEC_H
#define __ASM_AUXVEC_H

/* VDSO location */
#define AT_SYSINFO_EHDR		33

#ifdef __KERNEL__
/* number of entries ARCH_DLINFO adds to the auxiliary vector */
#define AT_VECTOR_SIZE_ARCH	1
#endif

#endif
