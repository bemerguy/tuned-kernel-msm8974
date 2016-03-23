/*
 * Media device node
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Based on drivers/media/video/v4l2_dev.c code authored by
 *	Mauro Carvalho Chehab <mchehab@infradead.org> (version 2)
 *	Alan Cox, <alan@lxorguk.ukuu.org.uk> (version 1)
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * --
 *
 * Generic media device node infrastructure to register and unregister
 * character devices using a dynamic major number and proper reference
 * counting.
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <media/media-devnode.h>

#define MEDIA_NUM_DEVICES	256
#define MEDIA_NAME		"media"

static dev_t media_dev_t;

/*
 *	Active devices
 */
static DEFINE_MUTEX(media_devnode_lock);
static DECLARE_BITMAP(media_devnode_nums, MEDIA_NUM_DEVICES);

/* Called when the last user of the media device exits. */
static void media_devnode_release(struct device *cd)
{
	struct media_devnode *devnode = to_media_devnode(cd);

	mutex_lock(&media_devnode_lock);

	/* Delete the cdev on this minor as well */
	cdev_del(&devnode->cdev);

	/* Mark device node number as free */
	clear_bit(devnode->minor, media_devnode_nums);

	mutex_unlock(&media_devnode_lock);

	/* Release media_devnode and perform other cleanups as needed. */
	if (devnode->release)
		devnode->release(devnode);
}

static struct bus_type media_bus_type = {
	.name = MEDIA_NAME,
};

static ssize_t media_read(struct file *filp, char __user *buf,
		size_t sz, loff_t *off)
{
	struct media_devnode *devnode = media_devnode_data(filp);

	if (!devnode->fops->read)
		return -EINVAL;
	if (!media_devnode_is_registered(devnode))
		return -EIO;
	return devnode->fops->read(filp, buf, sz, off);
}

static ssize_t media_write(struct file *filp, const char __user *buf,
		size_t sz, loff_t *off)
{
	struct media_devnode *devnode = media_devnode_data(filp);

	if (!devnode->fops->write)
		return -EINVAL;
	if (!media_devnode_is_registered(devnode))
		return -EIO;
	return devnode->fops->write(filp, buf, sz, off);
}

static unsigned int media_poll(struct file *filp,
			       struct poll_table_struct *poll)
{
	struct media_devnode *devnode = media_devnode_data(filp);

	if (!media_devnode_is_registered(devnode))
		return POLLERR | POLLHUP;
	if (!devnode->fops->poll)
		return DEFAULT_POLLMASK;
	return devnode->fops->poll(filp, poll);
}

static long media_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct media_devnode *devnode = media_devnode_data(filp);

	if (!devnode->fops->ioctl)
		return -ENOTTY;

	if (!media_devnode_is_registered(devnode))
		return -EIO;

	return devnode->fops->ioctl(filp, cmd, arg);
}

/* Override for the open function */
static int media_open(struct inode *inode, struct file *filp)
{
	struct media_devnode *devnode;
	int ret;

	/* Check if the media device is available. This needs to be done with
	 * the media_devnode_lock held to prevent an open/unregister race:
	 * without the lock, the device could be unregistered and freed between
	 * the media_devnode_is_registered() and get_device() calls, leading to
	 * a crash.
	 */
	mutex_lock(&media_devnode_lock);
	devnode = container_of(inode->i_cdev, struct media_devnode, cdev);
	/* return ENXIO if the media device has been removed
	   already or if it is not registered anymore. */
	if (!media_devnode_is_registered(devnode)) {
		mutex_unlock(&media_devnode_lock);
		return -ENXIO;
	}
	/* and increase the device refcount */
	get_device(&devnode->dev);
	mutex_unlock(&media_devnode_lock);

	filp->private_data = devnode;

	if (devnode->fops->open) {
		ret = devnode->fops->open(filp);
		if (ret) {
			put_device(&devnode->dev);
			filp->private_data = NULL;
			return ret;
		}
	}

	return 0;
}

/* Override for the release function */
static int media_release(struct inode *inode, struct file *filp)
{
	struct media_devnode *devnode = media_devnode_data(filp);

	if (devnode->fops->release)
		devnode->fops->release(filp);

	filp->private_data = NULL;

	/* decrease the refcount unconditionally since the release()
	   return value is ignored. */
	put_device(&devnode->dev);
	return 0;
}

static const struct file_operations media_devnode_fops = {
	.owner = THIS_MODULE,
	.read = media_read,
	.write = media_write,
	.open = media_open,
	.unlocked_ioctl = media_ioctl,
	.release = media_release,
	.poll = media_poll,
	.llseek = no_llseek,
};

/**
 * media_devnode_register - register a media device node
 * @devnode: media device node structure we want to register
 *
 * The registration code assigns minor numbers and registers the new device node
 * with the kernel. An error is returned if no free minor number can be found,
 * or if the registration of the device node fails.
 *
 * Zero is returned on success.
 *
 * Note that if the media_devnode_register call fails, the release() callback of
 * the media_devnode structure is *not* called, so the caller is responsible for
 * freeing any data.
 */
int __must_check media_devnode_register(struct media_devnode *devnode,
					struct module *owner)
{
	int minor;
	int ret;

	/* Part 1: Find a free minor number */
	mutex_lock(&media_devnode_lock);
	minor = find_next_zero_bit(media_devnode_nums, MEDIA_NUM_DEVICES, 0);
	if (minor == MEDIA_NUM_DEVICES) {
		mutex_unlock(&media_devnode_lock);
		printk(KERN_ERR "could not get a free minor\n");
		return -ENFILE;
	}

	set_bit(minor, media_devnode_nums);
	mutex_unlock(&media_devnode_lock);

	devnode->minor = minor;

	/* Part 2: Initialize and register the character device */
	cdev_init(&devnode->cdev, &media_devnode_fops);
	devnode->cdev.owner = owner;

	ret = cdev_add(&devnode->cdev, MKDEV(MAJOR(media_dev_t), devnode->minor), 1);
	if (ret < 0) {
		printk(KERN_ERR "%s: cdev_add failed\n", __func__);
		goto error;
	}

	/* Part 3: Register the media device */
	devnode->dev.bus = &media_bus_type;
	devnode->dev.devt = MKDEV(MAJOR(media_dev_t), devnode->minor);
	devnode->dev.release = media_devnode_release;
	if (devnode->parent)
		devnode->dev.parent = devnode->parent;
	dev_set_name(&devnode->dev, "media%d", devnode->minor);
	ret = device_register(&devnode->dev);
	if (ret < 0) {
		printk(KERN_ERR "%s: device_register failed\n", __func__);
		goto error;
	}

	/* Part 4: Activate this minor. The char device can now be used. */
	set_bit(MEDIA_FLAG_REGISTERED, &devnode->flags);

	return 0;

error:
	mutex_lock(&media_devnode_lock);
	cdev_del(&devnode->cdev);
	clear_bit(devnode->minor, media_devnode_nums);
	mutex_unlock(&media_devnode_lock);

	return ret;
}

/**
 * media_devnode_unregister - unregister a media device node
 * @devnode: the device node to unregister
 *
 * This unregisters the passed device. Future open calls will be met with
 * errors.
 *
 * This function can safely be called if the device node has never been
 * registered or has already been unregistered.
 */
void media_devnode_unregister(struct media_devnode *devnode)
{
	/* Check if devnode was ever registered at all */
	if (!media_devnode_is_registered(devnode))
		return;

	mutex_lock(&media_devnode_lock);
	clear_bit(MEDIA_FLAG_REGISTERED, &devnode->flags);
	mutex_unlock(&media_devnode_lock);
	device_unregister(&devnode->dev);
}

/*
 *	Initialise media for linux
 */
static int __init media_devnode_init(void)
{
	int ret;

	printk(KERN_INFO "Linux media interface: v0.10\n");
	ret = alloc_chrdev_region(&media_dev_t, 0, MEDIA_NUM_DEVICES,
				  MEDIA_NAME);
	if (ret < 0) {
		printk(KERN_WARNING "media: unable to allocate major\n");
		return ret;
	}

	ret = bus_register(&media_bus_type);
	if (ret < 0) {
		unregister_chrdev_region(media_dev_t, MEDIA_NUM_DEVICES);
		printk(KERN_WARNING "media: bus_register failed\n");
		return -EIO;
	}

	return 0;
}

static void __exit media_devnode_exit(void)
{
	bus_unregister(&media_bus_type);
	unregister_chrdev_region(media_dev_t, MEDIA_NUM_DEVICES);
}

subsys_initcall(media_devnode_init);
module_exit(media_devnode_exit)

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Device node registration for media drivers");
MODULE_LICENSE("GPL");
