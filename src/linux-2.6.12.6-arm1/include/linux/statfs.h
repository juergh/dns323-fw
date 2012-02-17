#ifndef _LINUX_STATFS_H
#define _LINUX_STATFS_H

#include <linux/types.h>

#include <asm/statfs.h>

struct kstatfs {
	long f_type;
	long f_bsize;
/*
 * wyc:
 *
 *   by Trond Myklebust:
 *      The type "sector_t" is heavily tied in to the block layer interface
 *      as an offset/handle to a block, and is subject to a supposedly
 *      block-specific configuration option: CONFIG_LBD. Despite this, it is
 *      used in struct kstatfs to save a couple of bytes on the stack
 *      whenever we call the filesystems' ->statfs().
 *
 *   So kstatfs's entries related to blocks are invalid on statfs64 for a
 *   network filesystem which has more than 2^32-1 blocks when CONFIG_LBD
 *   is disabled.
 */
#if 0
	sector_t f_blocks;
	sector_t f_bfree;
	sector_t f_bavail;
	sector_t f_files;
	sector_t f_ffree;
#endif
	u64 f_blocks;
	u64 f_bfree;
	u64 f_bavail;
	u64 f_files;
	u64 f_ffree;
	__kernel_fsid_t f_fsid;
	long f_namelen;
	long f_frsize;
	long f_spare[5];
};

#endif
