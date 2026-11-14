#
# vDSO: clock_gettime()/gettimeofday() without a syscall, reading the ARM
# architected timer's virtual counter from user space.
#
obj-vdso := vgettimeofday.o

# Build rules
targets := $(obj-vdso) vdso.so vdso.so.dbg vdso.lds
obj-vdso := $(addprefix $(obj)/, $(obj-vdso))

# the vDSO is a small shared object of its own: position independent, no
# LTO, no profiling or unwind tables, fast code
KBUILD_CFLAGS := $(filter-out $(CC_FLAGS_LTO) -pg -Os -funwind-tables \
		 -fno-pic,$(KBUILD_CFLAGS))
ccflags-y := -fPIC -fno-common -fno-builtin -fno-stack-protector \
	     -fno-unwind-tables -fno-asynchronous-unwind-tables \
	     -DDISABLE_BRANCH_PROFILING -O2
ifeq ($(LLVM),1)
ccflags-y += -mllvm -arm-global-merge=false
endif

VDSO_LDFLAGS := -shared -nostdlib -Bsymbolic --no-undefined \
		-soname=linux-vdso.so.1 -z max-page-size=4096 \
		-z common-page-size=4096 --hash-style=sysv --build-id

obj-y += vdso.o
extra-y += vdso.lds
CPPFLAGS_vdso.lds += -P -C -U$(ARCH) -D__ASSEMBLY__

# Force dependency
$(obj)/vdso.o : $(obj)/vdso.so

# Link rule for the .so file
$(obj)/vdso.so.dbg: $(src)/vdso.lds $(obj-vdso) FORCE
	$(call if_changed,vdsold)

# Strip rule for the .so file (not cmd_objcopy: the arch OBJCOPYFLAGS
# carry -O binary for the boot images). Like mainline's vdsomunge, clear
# EF_ARM_ABI_FLOAT_SOFT from e_flags (offset 36) so that hard-float user
# space accepts the vDSO too: it takes no floating point arguments.
$(obj)/vdso.so: $(obj)/vdso.so.dbg FORCE
	$(call if_changed,vdso_strip)

quiet_cmd_vdso_strip = STRIP   $@
      cmd_vdso_strip = $(OBJCOPY) -S $< $@ && \
		       printf '\000\000\000\005' | \
		       dd of=$@ bs=1 seek=36 conv=notrunc status=none

quiet_cmd_vdsold = VDSOL   $@
      cmd_vdsold = $(LD) $(VDSO_LDFLAGS) -T $(filter %.lds,$^) \
		   $(filter %.o,$^) -o $@
