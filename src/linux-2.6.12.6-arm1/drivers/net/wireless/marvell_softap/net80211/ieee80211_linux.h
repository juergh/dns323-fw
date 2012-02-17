/*-
 * Copyright (c) 2003, 2004 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id: ieee80211_linux.h,v 1.1.1.1 2009/08/19 08:02:21 jack Exp $
 */
#ifndef _NET80211_IEEE80211_LINUX_H_
#define _NET80211_IEEE80211_LINUX_H_

/*
 * Node locking definitions.
 */
typedef rwlock_t ieee80211_node_lock_t;
#define	IEEE80211_NODE_LOCK_INIT(_ic, _name)	rwlock_init(&(_ic)->ic_nodelock)
#define	IEEE80211_NODE_LOCK_DESTROY(_ic)
#define	IEEE80211_NODE_LOCK(_ic)	write_lock(&(_ic)->ic_nodelock)
#define	IEEE80211_NODE_UNLOCK(_ic)	write_unlock(&(_ic)->ic_nodelock)
#define	IEEE80211_NODE_LOCK_BH(_ic)	write_lock_bh(&(_ic)->ic_nodelock)
#define	IEEE80211_NODE_UNLOCK_BH(_ic)	write_unlock_bh(&(_ic)->ic_nodelock)
/* NB: beware, *_is_locked() are boguly defined for UP+!PREEMPT */
#if (defined(CONFIG_SMP) || defined(CONFIG_PREEMPT)) && defined(rwlock_is_locked)
#define	IEEE80211_NODE_LOCK_ASSERT(_ic) \
	KASSERT(rwlock_is_locked(&(_ic)->ic_nodelock), \
		("802.11 node not locked!"))
#else
#define	IEEE80211_NODE_LOCK_ASSERT(_ic)
#endif

/*
 * 802.1x state locking definitions.
 */
typedef spinlock_t eapol_lock_t;
#define	EAPOL_LOCK_INIT(_ec, _name)	spin_lock_init(&(_ec)->ec_lock)
#define	EAPOL_LOCK_DESTROY(_ec)
#define	EAPOL_LOCK(_ec)			spin_lock_bh(&(_ec)->ec_lock)
#define	EAPOL_UNLOCK(_ec)		spin_unlock_bh(&(_ec)->ec_lock)
/* NB: beware, *_is_locked() are boguly defined for UP+!PREEMPT */
#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT)
#define	EAPOL_LOCK_ASSERT(_ec) \
	KASSERT(spin_is_locked(&(_ec)->ec_lock), ("EAPOL not locked!"))
#else
#define	EAPOL_LOCK_ASSERT(_ec)
#endif

/*
 * 802.1x MAC ACL database locking definitions.
 */
typedef spinlock_t acl_lock_t;
#define	ACL_LOCK_INIT(_as, _name)	spin_lock_init(&(_as)->as_lock)
#define	ACL_LOCK_DESTROY(_as)
#define	ACL_LOCK(_as)			spin_lock_bh(&(_as)->as_lock)
#define	ACL_UNLOCK(_as)			spin_unlock_bh(&(_as)->as_lock)
/* NB: beware, *_is_locked() are boguly defined for UP+!PREEMPT */
#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT)
#define	ACL_LOCK_ASSERT(_as) \
	KASSERT(spin_is_locked(&(_as)->as_lock), ("ACL not locked!"))
#else
#define	ACL_LOCK_ASSERT(_as)
#endif

#define	M_LINK0		0x01		/* frame needs WEP encryption */

/*
 * Node reference counting definitions.
 *
 * ieee80211_node_initref	initialize the reference count to 1
 * ieee80211_node_incref	add a reference
 * ieee80211_node_decref	remove a reference
 * ieee80211_node_dectestref	remove a reference and return 1 if this
 *				is the last reference, otherwise 0
 * ieee80211_node_refcnt	reference count for printing (only)
 */
#define ieee80211_node_initref(_ni) \
	atomic_set(&(_ni)->ni_refcnt, 1)
#define ieee80211_node_incref(_ni) \
	atomic_inc(&(_ni)->ni_refcnt)
#define	ieee80211_node_decref(_ni) \
	atomic_dec(&(_ni)->ni_refcnt)
#define	ieee80211_node_dectestref(_ni) \
	atomic_dec_and_test(&(_ni)->ni_refcnt)
#define	ieee80211_node_refcnt(_ni)	(_ni)->ni_refcnt.counter

#define	le16toh(_x)	le16_to_cpu(_x)
#define	htole16(_x)	cpu_to_le16(_x)
#define	le32toh(_x)	le32_to_cpu(_x)
#define	htole32(_x)	cpu_to_le32(_x)
#define	be32toh(_x)	be32_to_cpu(_x)
#define	htobe32(_x)	cpu_to_be32(_x)

/*
 * Linux has no equivalents to malloc types so null these out.
 */
#define	MALLOC_DEFINE(type, shortdesc, longdesc)
#define	MALLOC_DECLARE(type)

/*
 * flags to malloc.
 */
#define	M_NOWAIT	0x0001		/* do not block */
#define	M_WAITOK	0x0002		/* ok to block */
#define	M_ZERO		0x0100		/* bzero the allocation */

static inline void *
ieee80211_malloc(size_t size, int flags)
{
	void *p = kmalloc(size, flags & M_NOWAIT ? GFP_ATOMIC : GFP_KERNEL);
	if (p && (flags & M_ZERO))
		memset(p, 0, size);
	return p;
}
#define	MALLOC(_ptr, cast, _size, _type, _flags) \
	((_ptr) = (cast)ieee80211_malloc(_size, _flags))
#define	FREE(addr, type)	kfree((addr))

/*
 * This unlikely to be popular but it dramatically reduces diffs.
 */
#define	printf	printk
struct ieee80211com;
extern	void if_printf(struct net_device *, const char *, ...);
extern	const char *ether_sprintf(const u_int8_t *);

/*
 * Queue write-arounds and support routines.
 */
extern	struct sk_buff *ieee80211_getmgtframe(u_int8_t **frm, u_int pktlen);
#define	IF_ENQUEUE(_q,_skb)	skb_queue_tail(_q,_skb)
#define	IF_DEQUEUE(_q,_skb)	(_skb = skb_dequeue(_q))
#define	_IF_QLEN(_q)		skb_queue_len(_q)
#define	IF_DRAIN(_q)		skb_queue_drain(_q)
extern	void skb_queue_drain(struct sk_buff_head *q);

extern	struct net_device_stats *ieee80211_getstats(struct net_device *);

#ifndef __MOD_INC_USE_COUNT
#define	_MOD_INC_USE(_m, _err)						\
	if (!try_module_get(_m)) {					\
		printk(KERN_WARNING "%s: try_module_get failed\n",	\
			__func__); \
		_err;							\
	}
#define	_MOD_DEC_USE(_m)		module_put(_m)
#else
#define	_MOD_INC_USE(_m, _err)	MOD_INC_USE_COUNT
#define	_MOD_DEC_USE(_m)	MOD_DEC_USE_COUNT
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
static inline u_int64_t
get_jiffies_64(void)
{
	return (u_int64_t) jiffies;		/* XXX not right */
}
#endif

#ifndef CLONE_KERNEL
/*
 * List of flags we want to share for kernel threads,
 * if only because they are not used by them anyway.
 */
#define CLONE_KERNEL	(CLONE_FS | CLONE_FILES | CLONE_SIGHAND)
#endif

#ifndef offset_in_page
#define	offset_in_page(p) ((unsigned long) (p) & ~PAGE_MASK)
#endif

#ifndef module_put_and_exit
#define module_put_and_exit(code) do {	\
	_MOD_DEC_USE(THIS_MODULE);	\
	do_exit(code);			\
} while (0)
#endif

/*
 * Linux uses __BIG_ENDIAN and __LITTLE_ENDIAN while BSD uses _foo
 * and an explicit _BYTE_ORDER.  Sorry, BSD got there first--define
 * things in the BSD way...
 */
#define	_LITTLE_ENDIAN	1234	/* LSB first: i386, vax */
#define	_BIG_ENDIAN	4321	/* MSB first: 68000, ibm, net */
#include <asm/byteorder.h>
#if defined(__LITTLE_ENDIAN)
#define	_BYTE_ORDER	_LITTLE_ENDIAN
#elif defined(__BIG_ENDIAN)
#define	_BYTE_ORDER	_BIG_ENDIAN
#else
#error "Please fix asm/byteorder.h"
#endif

#ifdef CONFIG_SYSCTL
/*
 * Deal with the sysctl handler api changing.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,8)
#define	IEEE80211_SYSCTL_DECL(f, ctl, write, filp, buffer, lenp, ppos) \
	f(ctl_table *ctl, int write, struct file *filp, void *buffer, \
		size_t *lenp)
#define	IEEE80211_SYSCTL_PROC_DOINTVEC(ctl, write, filp, buffer, lenp, ppos) \
	proc_dointvec(ctl, write, filp, buffer, lenp)
#else
#define	IEEE80211_SYSCTL_DECL(f, ctl, write, filp, buffer, lenp, ppos) \
	f(ctl_table *ctl, int write, struct file *filp, void *buffer,\
		size_t *lenp, loff_t *ppos)
#define	IEEE80211_SYSCTL_PROC_DOINTVEC(ctl, write, filp, buffer, lenp, ppos) \
	proc_dointvec(ctl, write, filp, buffer, lenp, ppos)
#endif

extern	void ieee80211_sysctl_register(struct ieee80211com *);
extern	void ieee80211_sysctl_unregister(struct ieee80211com *);
#endif /* CONFIG_SYSCTL */

#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
#define IEEE80211_VLAN_TAG_USED 1

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,20)
#define	vlan_hwaccel_receive_skb(skb, grp, tag)	vlan_hwaccel_rx(skb, grp, tag)
#endif

extern	void ieee80211_vlan_register(struct ieee80211com *, struct vlan_group*);
extern	void ieee80211_vlan_kill_vid(struct ieee80211com *, unsigned short);
#else
#define IEEE80211_VLAN_TAG_USED 0
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#define	free_netdev(dev)	kfree(dev)
#endif

#ifdef CONFIG_NET_WIRELESS
struct iw_statistics;
extern	void ieee80211_iw_getstats(struct ieee80211com*, struct iw_statistics*);
struct iw_request_info;
struct iw_point;
extern	int ieee80211_ioctl_giwname(struct ieee80211com *,
		struct iw_request_info *, char *name, char *extra);
extern	int ieee80211_ioctl_siwencode(struct ieee80211com *,
		struct iw_request_info *, struct iw_point *, char *);
extern	int ieee80211_ioctl_giwencode(struct ieee80211com *,
		struct iw_request_info *, struct iw_point *, char *key);
struct iw_param;
extern	int ieee80211_ioctl_siwrate(struct ieee80211com *,
		struct iw_request_info *, struct iw_param *, char *);
extern	int ieee80211_ioctl_giwrate(struct ieee80211com *,
		struct iw_request_info *, struct iw_param *, char *);
extern	int ieee80211_ioctl_siwsens(struct ieee80211com *,
		struct iw_request_info *, struct iw_param *, char *);
extern	int ieee80211_ioctl_giwsens(struct ieee80211com *,
		struct iw_request_info *, struct iw_param *, char *);
extern	int ieee80211_ioctl_siwrts(struct ieee80211com *,
		struct iw_request_info *, struct iw_param *, char *);
extern	int ieee80211_ioctl_giwrts(struct ieee80211com *,
		struct iw_request_info *, struct iw_param *, char *);
extern	int ieee80211_ioctl_siwfrag(struct ieee80211com *,
		struct iw_request_info *, struct iw_param *, char *);
extern	int ieee80211_ioctl_giwfrag(struct ieee80211com *,
		struct iw_request_info *, struct iw_param *, char *);
struct sockaddr;
extern	int ieee80211_ioctl_siwap(struct ieee80211com *,
		struct iw_request_info *, struct sockaddr *, char *);
extern	int ieee80211_ioctl_giwap(struct ieee80211com *,
		struct iw_request_info *, struct sockaddr *, char *);
extern	int ieee80211_ioctl_siwnickn(struct ieee80211com *,
		struct iw_request_info *, struct iw_point *, char *);
extern	int ieee80211_ioctl_giwnickn(struct ieee80211com *,
		struct iw_request_info *, struct iw_point *, char *);
struct iw_freq;
extern	int ieee80211_ioctl_siwfreq(struct ieee80211com *,
		struct iw_request_info *, struct iw_freq *, char *);
extern	int ieee80211_ioctl_giwfreq(struct ieee80211com *,
		struct iw_request_info *, struct iw_freq *, char *);
extern	int ieee80211_ioctl_siwessid(struct ieee80211com *,
		struct iw_request_info *, struct iw_point *, char *);
extern	int ieee80211_ioctl_giwessid(struct ieee80211com *,
		struct iw_request_info *, struct iw_point *, char *);
extern	int ieee80211_ioctl_giwrange(struct ieee80211com *,
		struct iw_request_info *, struct iw_point *, char *);
extern	int ieee80211_ioctl_siwmode(struct ieee80211com *,
		struct iw_request_info *, __u32 *, char *);
extern	int ieee80211_ioctl_giwmode(struct ieee80211com *,
		struct iw_request_info *, __u32 *, char *);
extern	int ieee80211_ioctl_siwpower(struct ieee80211com *,
		struct iw_request_info *, struct iw_param *, char *);
extern	int ieee80211_ioctl_giwpower(struct ieee80211com *,
		struct iw_request_info *, struct iw_param *, char *);
extern	int ieee80211_ioctl_siwretry(struct ieee80211com *,
		struct iw_request_info *, struct iw_param *, char *);
extern	int ieee80211_ioctl_giwretry(struct ieee80211com *,
		struct iw_request_info *, struct iw_param *, char *);
extern	int ieee80211_ioctl_siwtxpow(struct ieee80211com *,
		struct iw_request_info *, struct iw_param *, char *);
extern	int ieee80211_ioctl_giwtxpow(struct ieee80211com *,
		struct iw_request_info *, struct iw_param *, char *);
extern	int ieee80211_ioctl_iwaplist(struct ieee80211com *,
		struct iw_request_info *, struct iw_point *, char *);
extern	int ieee80211_ioctl_siwscan(struct ieee80211com *,
		struct iw_request_info *, struct iw_point *, char *);
extern	int ieee80211_ioctl_giwscan(struct ieee80211com *,
		struct iw_request_info *, struct iw_point *, char *);

extern	int ieee80211_ioctl_setparam(struct ieee80211com *,
		struct iw_request_info *, void *, char *);
extern	int ieee80211_ioctl_getparam(struct ieee80211com *,
		struct iw_request_info *, void *, char *);
extern	int ieee80211_ioctl_setoptie(struct ieee80211com *,
		struct iw_request_info *, void *, char *);
extern	int ieee80211_ioctl_getoptie(struct ieee80211com *,
		struct iw_request_info *, void *, char *);
extern	int ieee80211_ioctl_setkey(struct ieee80211com *,
		struct iw_request_info *, void *, char *);
extern	int ieee80211_ioctl_delkey(struct ieee80211com *,
		struct iw_request_info *, void *, char *);
extern	int ieee80211_ioctl_setmlme(struct ieee80211com *,
		struct iw_request_info *, void *, char *);
extern	int ieee80211_ioctl_addmac(struct ieee80211com *,
		struct iw_request_info *, void *, char *);
extern	int ieee80211_ioctl_delmac(struct ieee80211com *,
		struct iw_request_info *, void *, char *);
extern	int ieee80211_ioctl_chanlist(struct ieee80211com *,
		struct iw_request_info *, void *, char *);

extern	void ieee80211_ioctl_iwsetup(struct iw_handler_def *);
#endif

#endif /* _NET80211_IEEE80211_LINUX_H_ */
