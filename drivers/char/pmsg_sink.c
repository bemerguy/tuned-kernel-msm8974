/*
 * /dev/pmsg0 sink
 *
 * Android's liblog writes every log message to /dev/pmsg0 (the pstore
 * "persistent message" device, mainline 4.0) in addition to logd. This
 * kernel has no pmsg, so each log call in every process does an open()
 * of /dev/pmsg0 that fails with ENOENT. Besides the wasted syscalls,
 * the failed open still reserves the lowest free fd for a moment: in a
 * multithreaded process that logs a lot this shuffles fd numbers. qmuxd
 * assumes its client sockets get higher fds than its listening sockets;
 * when a log call from another thread holds a low fd at the wrong time,
 * a client lands below a listener, is never read and qmuxd spins in
 * select() at 100% CPU while rild fails to bring up QMI.
 *
 * Provide the device so liblog opens it once and keeps it. There is no
 * pstore backend here, so the data is discarded; logd still receives
 * every message.
 */
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/uio.h>

static ssize_t pmsg_sink_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	return count;
}

static ssize_t pmsg_sink_aio_write(struct kiocb *iocb, const struct iovec *iov,
				   unsigned long nr_segs, loff_t pos)
{
	return iov_length(iov, nr_segs);
}

static const struct file_operations pmsg_sink_fops = {
	.owner		= THIS_MODULE,
	.open		= nonseekable_open,
	.write		= pmsg_sink_write,
	.aio_write	= pmsg_sink_aio_write,
	.llseek		= no_llseek,
};

static struct miscdevice pmsg_sink_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "pmsg0",
	.fops	= &pmsg_sink_fops,
	.mode	= 0222,
};

static int __init pmsg_sink_init(void)
{
	return misc_register(&pmsg_sink_dev);
}
device_initcall(pmsg_sink_init);
