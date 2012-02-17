/*
 * $Id: physmap.c,v 1.1.1.1 2009/08/19 08:02:17 jack Exp $
 *
 * Normal mappings of chips in physical memory
 *
 * Copyright (C) 2003 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * 031022 - [jsun] add run-time configure and partition setup
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/config.h>
#include <linux/mtd/partitions.h>

static struct mtd_info *mymtd;

struct map_info physmap_map = {
	.name = "phys_mapped_flash",
	.phys = CONFIG_MTD_PHYSMAP_START,
	.size = CONFIG_MTD_PHYSMAP_LEN,
	.bankwidth = CONFIG_MTD_PHYSMAP_BANKWIDTH,
};

#ifdef CONFIG_MTD_PARTITIONS
static struct mtd_partition *mtd_parts;
static int                   mtd_parts_nb;

//+Wilson06192006
#if 0
static int num_physmap_partitions;
static struct mtd_partition *physmap_partitions;

static const char *part_probes[] __initdata = {"cmdlinepart", "RedBoot", NULL};

void physmap_set_partitions(struct mtd_partition *parts, int num_parts)
{
	physmap_partitions=parts;
	num_physmap_partitions=num_parts;
}

#else
	static struct mtd_partition physmap_partitions[] = {
	/* Put your own partition definitions here */
	{
		.name =		"MTD1",
		.size =		0x00010000,
		.offset =	0,
	}, {
		.name =		"MTD2",
		.size =		0x00010000,
		.offset =	0x00010000,
	}, {
		.name =		"Linux Kernel",
		.size =		0x00180000,
		.offset =	0x00020000,
	}, {
		.name =		"File System",
		.size =		0x00630000,
		.offset =	0x001A0000,
	}, {
		.name =		"u-boot",
		.size =		0x00030000,
		.offset =	0x007d0000,
	}
};

#define NUM_PARTITIONS	(sizeof(physmap_partitions)/sizeof(struct mtd_partition))
static int num_physmap_partitions = NUM_PARTITIONS;
static const char *part_probes[] __initdata = {"cmdlinepart", "RedBoot", NULL};
#endif
//Wilson06192006+

#endif /* CONFIG_MTD_PARTITIONS */

static int __init init_physmap(void)
{
	static const char *rom_probe_types[] = { "cfi_probe", "jedec_probe", "map_rom", NULL };
	const char **type;

//jack20060623+	
	physmap_map.phys = CONFIG_MTD_PHYSMAP_START;
	physmap_map.size = CONFIG_MTD_PHYSMAP_LEN;
	physmap_map.bankwidth = CONFIG_MTD_PHYSMAP_BANKWIDTH;
//jack20060623 end
	
       	printk(KERN_NOTICE "physmap flash device: %lx at %lx\n", physmap_map.size, physmap_map.phys);
	physmap_map.virt = ioremap(physmap_map.phys, physmap_map.size);

	if (!physmap_map.virt) {
		printk("Failed to ioremap\n");
		return -EIO;
	}

	simple_map_init(&physmap_map);

	mymtd = NULL;
	type = rom_probe_types;
	for(; !mymtd && *type; type++) {
		mymtd = do_map_probe(*type, &physmap_map);
	}
	if (mymtd) {
		mymtd->owner = THIS_MODULE;

#ifdef CONFIG_MTD_PARTITIONS
		mtd_parts_nb = parse_mtd_partitions(mymtd, part_probes, 
						    &mtd_parts, 0);

		if (mtd_parts_nb > 0)
		{
			add_mtd_partitions (mymtd, mtd_parts, mtd_parts_nb);
			return 0;
		}

		if (num_physmap_partitions != 0) 
		{
			printk(KERN_NOTICE 
			       "Using physmap partition definition\n");
			add_mtd_partitions (mymtd, physmap_partitions, num_physmap_partitions);
			return 0;
		}

#endif
		add_mtd_device(mymtd);

		return 0;
	}

	iounmap(physmap_map.virt);
	return -ENXIO;
}

static void __exit cleanup_physmap(void)
{
#ifdef CONFIG_MTD_PARTITIONS
	if (mtd_parts_nb) {
		del_mtd_partitions(mymtd);
		kfree(mtd_parts);
	} else if (num_physmap_partitions) {
		del_mtd_partitions(mymtd);
	} else {
		del_mtd_device(mymtd);
	}
#else
	del_mtd_device(mymtd);
#endif
	map_destroy(mymtd);

	iounmap(physmap_map.virt);
	physmap_map.virt = NULL;
}

module_init(init_physmap);
module_exit(cleanup_physmap);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("Generic configurable MTD map driver");
