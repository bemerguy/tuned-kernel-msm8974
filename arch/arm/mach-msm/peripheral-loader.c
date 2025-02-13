/* Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/elf.h>
#include <linux/mutex.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/rwsem.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/wakelock.h>
#include <linux/err.h>
#include <linux/msm_ion.h>
#include <linux/list.h>
#include <linux/list_sort.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>

#include <asm/uaccess.h>
#include <asm/setup.h>
#include <asm-generic/io-64-nonatomic-lo-hi.h>

#include <mach/msm_iomap.h>
#include <mach/ramdump.h>

#include "peripheral-loader.h"

#ifdef CONFIG_SEC_PERIPHERAL_SECURE_CHK
#include <mach/sec_debug.h>
#endif

/* This will be replaced by device specific configuration flag in future */
#ifdef CONFIG_ARCH_MSM8226
/* for hw_rev */
#include <mach/msm_smem.h>
#include <asm/io.h>
#endif 

extern int poweroff_charging;

#define pil_err(desc, fmt, ...)						\
	dev_err(desc->dev, "%s: " fmt, desc->name, ##__VA_ARGS__)
#define pil_info(desc, fmt, ...)					\
	dev_info(desc->dev, "%s: " fmt, desc->name, ##__VA_ARGS__)

#define PIL_IMAGE_INFO_BASE	(MSM_IMEM_BASE + 0x94c)

/* This will be replaced by device specific configuration flag in future */
#ifdef CONFIG_ARCH_MSM8226
extern unsigned int system_rev;
#endif

/**
 * proxy_timeout - Override for proxy vote timeouts
 * -1: Use driver-specified timeout
 *  0: Hold proxy votes until shutdown
 * >0: Specify a custom timeout in ms
 */
static int proxy_timeout_ms = -1;
module_param(proxy_timeout_ms, int, S_IRUGO | S_IWUSR);

/**
 * struct pil_mdt - Representation of <name>.mdt file in memory
 * @hdr: ELF32 header
 * @phdr: ELF32 program headers
 */
struct pil_mdt {
	struct elf32_hdr hdr;
	struct elf32_phdr phdr[];
};

/**
 * struct pil_seg - memory map representing one segment
 * @next: points to next seg mentor NULL if last segment
 * @paddr: start address of segment
 * @sz: size of segment
 * @filesz: size of segment on disk
 * @num: segment number
 * @relocated: true if segment is relocated, false otherwise
 *
 * Loosely based on an elf program header. Contains all necessary information
 * to load and initialize a segment of the image in memory.
 */
struct pil_seg {
	phys_addr_t paddr;
	unsigned long sz;
	unsigned long filesz;
	int num;
	struct list_head list;
	bool relocated;
};

/**
 * struct pil_image_info - information in IMEM about image and where it is loaded
 * @name: name of image (may or may not be NULL terminated)
 * @start: indicates physical address where image starts (little endian)
 * @size: size of image (little endian)
 */
struct pil_image_info {
	char name[8];
	__le64 start;
	__le32 size;
} __attribute__((__packed__));

/**
 * struct pil_priv - Private state for a pil_desc
 * @proxy: work item used to run the proxy unvoting routine
 * @wlock: wakelock to prevent suspend during pil_boot
 * @wname: name of @wlock
 * @desc: pointer to pil_desc this is private data for
 * @seg: list of segments sorted by physical address
 * @entry_addr: physical address where processor starts booting at
 * @base_addr: smallest start address among all segments that are relocatable
 * @region_start: address where relocatable region starts or lowest address
 * for non-relocatable images
 * @region_end: address where relocatable region ends or highest address for
 * non-relocatable images
 * @region: region allocated for relocatable images
 * @unvoted_flag: flag to keep track if we have unvoted or not.
 *
 * This struct contains data for a pil_desc that should not be exposed outside
 * of this file. This structure points to the descriptor and the descriptor
 * points to this structure so that PIL drivers can't access the private
 * data of a descriptor but this file can access both.
 */
struct pil_priv {
	struct delayed_work proxy;
	struct wake_lock wlock;
	char wname[32];
	struct pil_desc *desc;
	struct list_head segs;
	phys_addr_t entry_addr;
	phys_addr_t base_addr;
	phys_addr_t region_start;
	phys_addr_t region_end;
	struct ion_handle *region;
	struct pil_image_info __iomem *info;
	int id;
	int unvoted_flag;
};

/**
 * pil_do_ramdump() - Ramdump an image
 * @desc: descriptor from pil_desc_init()
 * @ramdump_dev: ramdump device returned from create_ramdump_device()
 *
 * Calls the ramdump API with a list of segments generated from the addresses
 * that the descriptor corresponds to.
 */
int pil_do_ramdump(struct pil_desc *desc, void *ramdump_dev)
{
	struct pil_priv *priv = desc->priv;
	struct pil_seg *seg;
	int count = 0, ret;
	struct ramdump_segment *ramdump_segs, *s;

	list_for_each_entry(seg, &priv->segs, list)
		count++;

	ramdump_segs = kmalloc_array(count, sizeof(*ramdump_segs), GFP_KERNEL);
	if (!ramdump_segs)
		return -ENOMEM;

	s = ramdump_segs;
	list_for_each_entry(seg, &priv->segs, list) {
		s->address = seg->paddr;
		s->size = seg->sz;
		s++;
	}

	ret = do_elf_ramdump(ramdump_dev, ramdump_segs, count);
	kfree(ramdump_segs);

	return ret;
}
EXPORT_SYMBOL(pil_do_ramdump);

static struct ion_client *ion;

/**
 * pil_get_entry_addr() - Retrieve the entry address of a peripheral image
 * @desc: descriptor from pil_desc_init()
 *
 * Returns the physical address where the image boots at or 0 if unknown.
 */
phys_addr_t pil_get_entry_addr(struct pil_desc *desc)
{
	return desc->priv ? desc->priv->entry_addr : 0;
}
EXPORT_SYMBOL(pil_get_entry_addr);

static void __pil_proxy_unvote(struct pil_priv *priv)
{
	struct pil_desc *desc = priv->desc;

	desc->ops->proxy_unvote(desc);
	wake_unlock(&priv->wlock);
	module_put(desc->owner);

}

static void pil_proxy_unvote_work(struct work_struct *work)
{
	struct delayed_work *delayed = to_delayed_work(work);
	struct pil_priv *priv = container_of(delayed, struct pil_priv, proxy);
	__pil_proxy_unvote(priv);
}

static int pil_proxy_vote(struct pil_desc *desc)
{
	int ret = 0;
	struct pil_priv *priv = desc->priv;

	if (desc->ops->proxy_vote) {
		wake_lock(&priv->wlock);
		ret = desc->ops->proxy_vote(desc);
		if (ret)
			wake_unlock(&priv->wlock);
	}

	if (desc->proxy_unvote_irq)
		enable_irq(desc->proxy_unvote_irq);

	return ret;
}

static void pil_proxy_unvote(struct pil_desc *desc, int immediate)
{
	struct pil_priv *priv = desc->priv;
	unsigned long timeout;

	if (proxy_timeout_ms == 0 && !immediate)
		return;
	else if (proxy_timeout_ms > 0)
		timeout = proxy_timeout_ms;
	else
		timeout = desc->proxy_timeout;

	if (desc->ops->proxy_unvote) {
		if (WARN_ON(!try_module_get(desc->owner)))
			return;

		if (immediate)
			timeout = 0;

		if (!desc->proxy_unvote_irq || immediate)
			schedule_delayed_work(&priv->proxy,
					      msecs_to_jiffies(timeout));
	}
}

static irqreturn_t proxy_unvote_intr_handler(int irq, void *dev_id)
{
	struct pil_desc *desc = dev_id;
	struct pil_priv *priv = desc->priv;

	pil_info(desc, "Power/Clock ready interrupt received\n");
	if (!desc->priv->unvoted_flag) {
		desc->priv->unvoted_flag = 1;
		__pil_proxy_unvote(priv);
	}

	return IRQ_HANDLED;
}

static bool segment_is_relocatable(const struct elf32_phdr *p)
{
	return !!(p->p_flags & BIT(27));
}

static phys_addr_t pil_reloc(const struct pil_priv *priv, phys_addr_t addr)
{
	return addr - priv->base_addr + priv->region_start;
}

static struct pil_seg *pil_init_seg(const struct pil_desc *desc,
				  const struct elf32_phdr *phdr, int num)
{
	bool reloc = segment_is_relocatable(phdr);
	const struct pil_priv *priv = desc->priv;
	struct pil_seg *seg;

	if (!reloc && memblock_overlaps_memory(phdr->p_paddr, phdr->p_memsz)) {
		pil_err(desc, "kernel memory would be overwritten [%#08lx, %#08lx)\n",
			(unsigned long)phdr->p_paddr,
			(unsigned long)(phdr->p_paddr + phdr->p_memsz));
		return ERR_PTR(-EPERM);
	}

	seg = kmalloc(sizeof(*seg), GFP_KERNEL);
	if (!seg)
		return ERR_PTR(-ENOMEM);
	seg->num = num;
	seg->paddr = reloc ? pil_reloc(priv, phdr->p_paddr) : phdr->p_paddr;
	seg->filesz = phdr->p_filesz;
	seg->sz = phdr->p_memsz;
	seg->relocated = reloc;
	INIT_LIST_HEAD(&seg->list);

	return seg;
}

#define segment_is_hash(flag) (((flag) & (0x7 << 24)) == (0x2 << 24))

static int segment_is_loadable(const struct elf32_phdr *p)
{
	return (p->p_type == PT_LOAD) && !segment_is_hash(p->p_flags) &&
		p->p_memsz;
}

static void pil_dump_segs(const struct pil_priv *priv)
{
	struct pil_seg *seg;
	phys_addr_t seg_h_paddr;

	list_for_each_entry(seg, &priv->segs, list) {
		seg_h_paddr = seg->paddr + seg->sz;
		pil_info(priv->desc, "%d: %pa %pa\n", seg->num,
				&seg->paddr, &seg_h_paddr);
	}
}

/*
 * Ensure the entry address lies within the image limits and if the image is
 * relocatable ensure it lies within a relocatable segment.
 */
static int pil_init_entry_addr(struct pil_priv *priv, const struct pil_mdt *mdt)
{
	struct pil_seg *seg;
	phys_addr_t entry = mdt->hdr.e_entry;
	bool image_relocated = priv->region;

	if (image_relocated)
		entry = pil_reloc(priv, entry);
	priv->entry_addr = entry;

	if (priv->desc->flags & PIL_SKIP_ENTRY_CHECK)
		return 0;

	list_for_each_entry(seg, &priv->segs, list) {
		if (entry >= seg->paddr && entry < seg->paddr + seg->sz) {
			if (!image_relocated)
				return 0;
			else if (seg->relocated)
				return 0;
		}
	}
	pil_err(priv->desc, "entry address %pa not within range\n", &entry);
	pil_dump_segs(priv);
	return -EADDRNOTAVAIL;
}

static int pil_alloc_region(struct pil_priv *priv, phys_addr_t min_addr,
				phys_addr_t max_addr, size_t align)
{
	struct ion_handle *region;
	int ret;
	unsigned int mask;
	size_t size = max_addr - min_addr;

	/* Don't reallocate due to fragmentation concerns, just sanity check */
	if (priv->region) {
		if (WARN(priv->region_end - priv->region_start < size,
			"Can't reuse PIL memory, too small\n"))
			return -ENOMEM;
		return 0;
	}

	if (!ion) {
		WARN_ON_ONCE("No ION client, can't support relocation\n");
		return -ENOMEM;
	}

	/* Force alignment due to linker scripts not getting it right */
	if (align > SZ_1M) {
		mask = ION_HEAP(ION_PIL2_HEAP_ID);
		align = SZ_4M;
	} else {
		mask = ION_HEAP(ION_PIL1_HEAP_ID);
		align = SZ_1M;
	}

	region = ion_alloc(ion, size, align, mask, 0);
	if (IS_ERR(region)) {
		pil_err(priv->desc, "Failed to allocate relocatable region\n");
		return PTR_ERR(region);
	}

	ret = ion_phys(ion, region, (ion_phys_addr_t *)&priv->region_start,
			&size);
	if (ret) {
		ion_free(ion, region);
		return ret;
	}

	priv->region = region;
	priv->region_end = priv->region_start + size;
	priv->base_addr = min_addr;

	return 0;
}

static int pil_setup_region(struct pil_priv *priv, const struct pil_mdt *mdt)
{
	const struct elf32_phdr *phdr;
	phys_addr_t min_addr_r, min_addr_n, max_addr_r, max_addr_n, start, end;
	size_t align = 0;
	int i, ret = 0;
	bool relocatable = false;

	min_addr_n = min_addr_r = (phys_addr_t)ULLONG_MAX;
	max_addr_n = max_addr_r = 0;

	/* Find the image limits */
	for (i = 0; i < mdt->hdr.e_phnum; i++) {
		phdr = &mdt->phdr[i];
		if (!segment_is_loadable(phdr))
			continue;

		start = phdr->p_paddr;
		end = start + phdr->p_memsz;

		if (segment_is_relocatable(phdr)) {
			min_addr_r = min(min_addr_r, start);
			max_addr_r = max(max_addr_r, end);
			/*
			 * Lowest relocatable segment dictates alignment of
			 * relocatable region
			 */
			if (min_addr_r == start)
				align = phdr->p_align;
			relocatable = true;
		} else {
			min_addr_n = min(min_addr_n, start);
			max_addr_n = max(max_addr_n, end);
		}

	}

	/*
	 * Align the max address to the next 4K boundary to satisfy iommus and
	 * XPUs that operate on 4K chunks.
	 */
	max_addr_n = ALIGN(max_addr_n, SZ_4K);
	max_addr_r = ALIGN(max_addr_r, SZ_4K);

	if (relocatable) {
		ret = pil_alloc_region(priv, min_addr_r, max_addr_r, align);
	} else {
		priv->region_start = min_addr_n;
		priv->region_end = max_addr_n;
		priv->base_addr = min_addr_n;
	}

	writeq(priv->region_start, &priv->info->start);
	writel_relaxed(priv->region_end - priv->region_start,
			&priv->info->size);

	return ret;
}

static int pil_cmp_seg(void *priv, struct list_head *a, struct list_head *b)
{
	struct pil_seg *seg_a = list_entry(a, struct pil_seg, list);
	struct pil_seg *seg_b = list_entry(b, struct pil_seg, list);

	return seg_a->paddr - seg_b->paddr;
}

static int pil_init_mmap(struct pil_desc *desc, const struct pil_mdt *mdt)
{
	struct pil_priv *priv = desc->priv;
	const struct elf32_phdr *phdr;
	struct pil_seg *seg;
	int i, ret;

	ret = pil_setup_region(priv, mdt);
	if (ret)
		return ret;

	pil_info(desc, "loading from %pa to %pa\n", &priv->region_start,
							&priv->region_end);

	for (i = 0; i < mdt->hdr.e_phnum; i++) {
		phdr = &mdt->phdr[i];
		if (!segment_is_loadable(phdr))
			continue;

		seg = pil_init_seg(desc, phdr, i);
		if (IS_ERR(seg))
			return PTR_ERR(seg);

		list_add_tail(&seg->list, &priv->segs);
	}
	list_sort(NULL, &priv->segs, pil_cmp_seg);

	return pil_init_entry_addr(priv, mdt);
}

static void pil_release_mmap(struct pil_desc *desc)
{
	struct pil_priv *priv = desc->priv;
	struct pil_seg *p, *tmp;

	writeq(0, &priv->info->start);
	writel_relaxed(0, &priv->info->size);

	list_for_each_entry_safe(p, tmp, &priv->segs, list) {
		list_del(&p->list);
		kfree(p);
	}
}

#define IOMAP_SIZE SZ_1M

static int pil_load_seg(struct pil_desc *desc, struct pil_seg *seg)
{
	int ret = 0, count;
	phys_addr_t paddr;
	char fw_name[30];
	const struct firmware *fw = NULL;
	const u8 *data;
	int num = seg->num;

	if (seg->filesz) {
		snprintf(fw_name, ARRAY_SIZE(fw_name), "%s.b%02d",
				desc->name, num);
		ret = request_firmware(&fw, fw_name, desc->dev);
		if (ret) {
			pil_err(desc, "Failed to locate blob %s\n", fw_name);
			return ret;
		}

		if (fw->size != seg->filesz) {
			pil_err(desc, "Blob size %u doesn't match %lu\n",
					fw->size, seg->filesz);
			ret = -EPERM;
			goto release_fw;
		}
	}

	/* Load the segment into memory */
	count = seg->filesz;
	paddr = seg->paddr;
	data = fw ? fw->data : NULL;
	while (count > 0) {
		int size;
		u8 __iomem *buf;

		size = min_t(size_t, IOMAP_SIZE, count);
		buf = ioremap(paddr, size);
		if (!buf) {
			pil_err(desc, "Failed to map memory\n");
			ret = -ENOMEM;
			goto release_fw;
		}
		memcpy(buf, data, size);
		iounmap(buf);

		count -= size;
		paddr += size;
		data += size;
	}

	/* Zero out trailing memory */
	count = seg->sz - seg->filesz;
	while (count > 0) {
		int size;
		u8 __iomem *buf;

		size = min_t(size_t, IOMAP_SIZE, count);
		buf = ioremap(paddr, size);
		if (!buf) {
			pil_err(desc, "Failed to map memory\n");
			ret = -ENOMEM;
			goto release_fw;
		}
		memset(buf, 0, size);
		iounmap(buf);

		count -= size;
		paddr += size;
	}

	if (desc->ops->verify_blob) {
		ret = desc->ops->verify_blob(desc, seg->paddr, seg->sz);
		if (ret)
			pil_err(desc, "Blob%u failed verification\n", num);
	}

release_fw:
	release_firmware(fw);
	return ret;
}

static void pil_parse_devicetree(struct pil_desc *desc)
{
	int clk_ready = 0;

	if (desc->ops->proxy_unvote &&
		of_find_property(desc->dev->of_node,
				"qcom,gpio-proxy-unvote",
				NULL)) {
		clk_ready = of_get_named_gpio(desc->dev->of_node,
				"qcom,gpio-proxy-unvote", 0);

		if (clk_ready < 0) {
			dev_err(desc->dev,
				"[%s]: Error getting proxy unvoting gpio\n",
				desc->name);
			return;
		}

		clk_ready = gpio_to_irq(clk_ready);
		if (clk_ready < 0) {
			dev_err(desc->dev,
				"[%s]: Error getting proxy unvote IRQ\n",
				desc->name);
			return;
		}
	}
	desc->proxy_unvote_irq = clk_ready;
}

/* Synchronize request_firmware() with suspend */
static DECLARE_RWSEM(pil_pm_rwsem);

/**
 * pil_boot() - Load a peripheral image into memory and boot it
 * @desc: descriptor from pil_desc_init()
 *
 * Returns 0 on success or -ERROR on failure.
 */
int pil_boot(struct pil_desc *desc)
{
	int ret;
	char fw_name[30];
	const struct pil_mdt *mdt;
	const struct elf32_hdr *ehdr;
	struct pil_seg *seg;
	const struct firmware *fw;
	struct pil_priv *priv = desc->priv;
#ifdef CONFIG_SEC_PERIPHERAL_SECURE_CHK
	static int load_count_fwd;
#endif

	/* Reinitialize for new image */
	pil_release_mmap(desc);

	down_read(&pil_pm_rwsem);
	snprintf(fw_name, sizeof(fw_name), "%s.mdt", desc->name);
	if ( poweroff_charging && strcmp("mba.mdt",fw_name)==0)   /* skip locating mba.mdt during poweroff charging */
		ret = -2; 
	else	
		ret = request_firmware(&fw, fw_name, desc->dev);
	if (ret) {
		pil_err(desc, "Failed to locate %s\n", fw_name);
		goto out;
	}

	if (fw->size < sizeof(*ehdr)) {
		pil_err(desc, "Not big enough to be an elf header\n");
		ret = -EIO;
		goto release_fw;
	}

	mdt = (const struct pil_mdt *)fw->data;
	ehdr = &mdt->hdr;

	if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG)) {
		pil_err(desc, "Not an elf header\n");
		ret = -EIO;
		goto release_fw;
	}

	if (ehdr->e_phnum == 0) {
		pil_err(desc, "No loadable segments\n");
		ret = -EIO;
		goto release_fw;
	}
	if (sizeof(struct elf32_phdr) * ehdr->e_phnum +
	    sizeof(struct elf32_hdr) > fw->size) {
		pil_err(desc, "Program headers not within mdt\n");
		ret = -EIO;
		goto release_fw;
	}

	ret = pil_init_mmap(desc, mdt);
	if (ret)
		goto release_fw;

	if (desc->ops->init_image)
		ret = desc->ops->init_image(desc, fw->data, fw->size);
	if (ret) {
		pil_err(desc, "Invalid firmware metadata\n");
#ifdef CONFIG_SEC_PERIPHERAL_SECURE_CHK
		if ((!strcmp(desc->name, "mba")) || (!strcmp(desc->name, "modem"))) {
			if (++load_count_fwd > 5) {
				release_firmware(fw);
				up_read(&pil_pm_rwsem);

				if (priv->region) {
					ion_free(ion, priv->region);
					priv->region = NULL;
				}

				pil_release_mmap(desc);
				sec_peripheral_secure_check_fail();
			}
		}
#endif
		goto release_fw;
	}

	if (desc->ops->mem_setup)
		ret = desc->ops->mem_setup(desc, priv->region_start,
				priv->region_end - priv->region_start);
	if (ret) {
		pil_err(desc, "Memory setup error\n");
		goto release_fw;
	}

	list_for_each_entry(seg, &desc->priv->segs, list) {
		ret = pil_load_seg(desc, seg);
		if (ret)
			goto release_fw;
	}

	desc->priv->unvoted_flag = 0;
	ret = pil_proxy_vote(desc);
	if (ret) {
		pil_err(desc, "Failed to proxy vote\n");
		goto release_fw;
	}

	ret = desc->ops->auth_and_reset(desc);
	if (ret) {
		pil_err(desc, "Failed to bring out of reset\n");
#ifdef CONFIG_SEC_PERIPHERAL_SECURE_CHK
		if ((!strcmp(desc->name, "mba")) || (!strcmp(desc->name, "modem"))) {
			pil_proxy_unvote(desc, ret);
			release_firmware(fw);
			up_read(&pil_pm_rwsem);
			if (priv->region) {
				ion_free(ion, priv->region);
				priv->region = NULL;
			}

			pil_release_mmap(desc);
			sec_peripheral_secure_check_fail();
		}
#endif
		goto err_boot;
	}
	pil_info(desc, "Brought out of reset\n");
err_boot:
	pil_proxy_unvote(desc, ret);
release_fw:
	release_firmware(fw);
out:
	up_read(&pil_pm_rwsem);
	if (ret) {
		if (priv->region) {
			ion_free(ion, priv->region);
			priv->region = NULL;
		}
		pil_release_mmap(desc);
	}
	return ret;
}
EXPORT_SYMBOL(pil_boot);

/**
 * pil_shutdown() - Shutdown a peripheral
 * @desc: descriptor from pil_desc_init()
 */
void pil_shutdown(struct pil_desc *desc)
{
	struct pil_priv *priv = desc->priv;

	if (desc->ops->shutdown)
		desc->ops->shutdown(desc);

	if (desc->proxy_unvote_irq) {
		disable_irq(desc->proxy_unvote_irq);
		if (!desc->priv->unvoted_flag)
			pil_proxy_unvote(desc, 1);
		return;
	}

	if (!proxy_timeout_ms)
		pil_proxy_unvote(desc, 1);
	else
		flush_delayed_work(&priv->proxy);
}
EXPORT_SYMBOL(pil_shutdown);

static DEFINE_IDA(pil_ida);

/**
 * pil_desc_init() - Initialize a pil descriptor
 * @desc: descriptor to intialize
 *
 * Initialize a pil descriptor for use by other pil functions. This function
 * must be called before calling pil_boot() or pil_shutdown().
 *
 * Returns 0 for success and -ERROR on failure.
 */
int pil_desc_init(struct pil_desc *desc)
{
	struct pil_priv *priv;
	int ret;
	void __iomem *addr;
	char buf[sizeof(priv->info->name)];

	if (WARN(desc->ops->proxy_unvote && !desc->ops->proxy_vote,
				"Invalid proxy voting. Ignoring\n"))
		((struct pil_reset_ops *)desc->ops)->proxy_unvote = NULL;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	desc->priv = priv;
	priv->desc = desc;

	priv->id = ret = ida_simple_get(&pil_ida, 0, 10, GFP_KERNEL);
	if (priv->id < 0)
		goto err;

	addr = PIL_IMAGE_INFO_BASE + sizeof(struct pil_image_info) * priv->id;
	priv->info = (struct pil_image_info __iomem *)addr;

	memcpy(buf, desc->name, sizeof(buf));
	__iowrite32_copy(priv->info->name, buf, sizeof(buf) / 4);

	pil_parse_devicetree(desc);

	/* Ignore users who don't make any sense */
	WARN(desc->ops->proxy_unvote && desc->proxy_unvote_irq == 0
		 && !desc->proxy_timeout,
		 "Invalid proxy unvote callback or a proxy timeout of 0"
		 " was specified or no proxy unvote IRQ was specified.\n");

	if (desc->proxy_unvote_irq) {
		ret = request_threaded_irq(desc->proxy_unvote_irq,
				  NULL,
				  proxy_unvote_intr_handler,
				  IRQF_TRIGGER_RISING,
				  desc->name, desc);
		if (ret < 0) {
			dev_err(desc->dev,
				"Unable to request proxy unvote IRQ: %d\n",
				ret);
			goto err;
		}
		disable_irq(desc->proxy_unvote_irq);
	}

	snprintf(priv->wname, sizeof(priv->wname), "pil-%s", desc->name);
	wake_lock_init(&priv->wlock, WAKE_LOCK_SUSPEND, priv->wname);
	INIT_DELAYED_WORK(&priv->proxy, pil_proxy_unvote_work);
	INIT_LIST_HEAD(&priv->segs);

	return 0;
err:
	kfree(priv);
	return ret;
}
EXPORT_SYMBOL(pil_desc_init);

/**
 * pil_desc_release() - Release a pil descriptor
 * @desc: descriptor to free
 */
void pil_desc_release(struct pil_desc *desc)
{
	struct pil_priv *priv = desc->priv;

	if (priv) {
		ida_simple_remove(&pil_ida, priv->id);
		flush_delayed_work(&priv->proxy);
		wake_lock_destroy(&priv->wlock);
	}
	desc->priv = NULL;
	kfree(priv);
}
EXPORT_SYMBOL(pil_desc_release);

static int pil_pm_notify(struct notifier_block *b, unsigned long event, void *p)
{
	switch (event) {
	case PM_SUSPEND_PREPARE:
		down_write(&pil_pm_rwsem);
		break;
	case PM_POST_SUSPEND:
		up_write(&pil_pm_rwsem);
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block pil_pm_notifier = {
	.notifier_call = pil_pm_notify,
};

/* This will be replaced by device specific configuration flag in future */
#ifdef CONFIG_ARCH_MSM8226
void write_hw_rev_to_smem(unsigned int hw_rev)
{
	unsigned int *hw_rev_sdata = NULL;
	hw_rev_sdata = smem_alloc2(SMEM_ID_VENDOR1, sizeof(unsigned int));
	if(unlikely(!hw_rev_sdata)) {
		pr_err("Could not allocate SMEM region to write hardware revision\n");
		return;
	}
	/* write hw_rev to smem region */
	writel_relaxed(hw_rev, hw_rev_sdata); 
	pr_info("hw_rev just written = %u\n", readl_relaxed(hw_rev_sdata));
}
#endif

static int __init msm_pil_init(void)
{
	ion = msm_ion_client_create(UINT_MAX, "pil");
	if (IS_ERR(ion)) /* Can't support relocatable images */
		ion = NULL;
#ifdef CONFIG_ARCH_MSM8226
	write_hw_rev_to_smem(system_rev);
#endif
	return register_pm_notifier(&pil_pm_notifier);
}
device_initcall(msm_pil_init);

static void __exit msm_pil_exit(void)
{
	unregister_pm_notifier(&pil_pm_notifier);
	if (ion)
		ion_client_destroy(ion);
}
module_exit(msm_pil_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Load peripheral images and bring peripherals out of reset");
