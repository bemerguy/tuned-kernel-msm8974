/*
 * Minimal bpf(2) stub for Android 14 on this 3.4 kernel.
 *
 * There is no eBPF here; every bpf() call fails with -ENOSYS, as before,
 * except BPF_OBJ_GET of the two gpuWork maps.  Android 14's gpuservice
 * (libgpuwork, GpuWork::initialize) aborts when it cannot open those
 * pinned maps, and init restarts it every ~5 s forever, writing a
 * tombstone each time.  On kernels older than 4.14 libgpuwork does not
 * inspect the map fd, and it gives up quietly when the pinned program
 * (prog_gpuWork_*) cannot be opened, so handing it an inert fd for the
 * maps is enough to let it start and stay idle.
 *
 * Every other command and path (netd, ConnectivityService, GpuMem, ...)
 * still gets -ENOSYS, so their behaviour does not change.
 */
#include <linux/anon_inodes.h>
#include <linux/err.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

#define BPF_OBJ_GET_CMD		7

static const char * const bpf_stub_maps[] = {
	"/sys/fs/bpf/map_gpuWork_gpu_work_map",
	"/sys/fs/bpf/map_gpuWork_gpu_work_global_data",
};

static const struct file_operations bpf_stub_fops = {
	.llseek		= noop_llseek,
};

SYSCALL_DEFINE3(bpf, int, cmd, void __user *, uattr, unsigned int, size)
{
	char path[64];
	u64 upath;
	long len;
	int i;

	if (cmd != BPF_OBJ_GET_CMD || size < sizeof(upath))
		return -ENOSYS;

	/* union bpf_attr: BPF_OBJ_GET starts with __aligned_u64 pathname */
	if (copy_from_user(&upath, uattr, sizeof(upath)))
		return -EFAULT;

	len = strncpy_from_user(path,
			(const char __user *)(unsigned long)upath, sizeof(path));
	if (len < 0)
		return len;
	if (len == sizeof(path))
		return -ENOSYS;

	for (i = 0; i < ARRAY_SIZE(bpf_stub_maps); i++)
		if (!strcmp(path, bpf_stub_maps[i]))
			return anon_inode_getfd("bpf-map", &bpf_stub_fops, NULL,
						O_RDWR | O_CLOEXEC);

	return -ENOSYS;
}
