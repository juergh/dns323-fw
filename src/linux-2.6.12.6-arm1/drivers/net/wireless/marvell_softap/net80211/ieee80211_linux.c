/*-
 * Copyright (c) 2003-2004 Sam Leffler, Errno Consulting
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
 * $Id: ieee80211_linux.c,v 1.1.1.1 2009/08/19 08:02:21 jack Exp $
 */
#ifndef EXPORT_SYMTAB
#define	EXPORT_SYMTAB
#endif

/*
 * IEEE 802.11 support (Linux-specific code)
 */
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/sysctl.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>

#include <net/iw_handler.h>
#include <linux/wireless.h>
#include <linux/if_arp.h>		/* XXX for ARPHRD_* */

#include <asm/uaccess.h>

#include "if_media.h"
#include "if_ethersubr.h"

#include <net80211/ieee80211_var.h>

/*
 * Print a console message with the device name prepended.
 */
void
if_printf(struct net_device *dev, const char *fmt, ...)
{
	va_list ap;
	char buf[512];		/* XXX */

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	printk("%s: %s", dev->name, buf);
}

/*
 * Allocate and setup a management frame of the specified
 * size.  We return the sk_buff and a pointer to the start
 * of the contiguous data area that's been reserved based
 * on the packet length.  The data area is forced to 32-bit
 * alignment and the buffer length to a multiple of 4 bytes.
 * This is done mainly so beacon frames (that require this)
 * can use this interface too.
 */
struct sk_buff *
ieee80211_getmgtframe(u_int8_t **frm, u_int pktlen)
{
	const u_int align = sizeof(u_int32_t);
	struct ieee80211_cb *cb;
	struct sk_buff *skb;
	u_int len;

	len = roundup(sizeof(struct ieee80211_frame) + pktlen, 4);
	skb = dev_alloc_skb(len + align-1);
	if (skb != NULL) {
		u_int off = ((unsigned long) skb->data) % align;
		if (off != 0)
			skb_reserve(skb, align - off);

		cb = (struct ieee80211_cb *)skb->cb;
		cb->ni = NULL;
		cb->flags = 0;

		skb_reserve(skb, sizeof(struct ieee80211_frame));
		*frm = skb_put(skb, pktlen);
	}
	return skb;
}

/*
 * Drain a queue of sk_buff's.
 */
void
skb_queue_drain(struct sk_buff_head *q)
{
	struct sk_buff *skb;
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags);
	while ((skb = __skb_dequeue(q)) != NULL)
		dev_kfree_skb(skb);
	spin_unlock_irqrestore(&q->lock, flags);
}

#if IEEE80211_VLAN_TAG_USED
/*
 * VLAN support.
 */

/*
 * Register a vlan group.
 */
void
ieee80211_vlan_register(struct ieee80211com *ic, struct vlan_group *grp)
{
	ic->ic_vlgrp = grp;
}
EXPORT_SYMBOL(ieee80211_vlan_register);

/*
 * Kill (i.e. delete) a vlan identifier.
 */
void
ieee80211_vlan_kill_vid(struct ieee80211com *ic, unsigned short vid)
{
	if (ic->ic_vlgrp)
		ic->ic_vlgrp->vlan_devices[vid] = NULL;
}
EXPORT_SYMBOL(ieee80211_vlan_kill_vid);
#endif /* IEEE80211_VLAN_TAG_USED */

void
ieee80211_notify_node_join(struct ieee80211com *ic, struct ieee80211_node *ni, int newassoc)
{
	union iwreq_data wreq;

	if (ni == ic->ic_bss) {
		if (newassoc)
			netif_carrier_on(ic->ic_dev);
		memset(&wreq, 0, sizeof(wreq));
		IEEE80211_ADDR_COPY(wreq.addr.sa_data, ni->ni_bssid);
		wreq.addr.sa_family = ARPHRD_ETHER;
		wireless_send_event(ic->ic_dev, SIOCGIWAP, &wreq, NULL);
	} else if (newassoc) {
		/* fire off wireless event only for new station */
		memset(&wreq, 0, sizeof(wreq));
		IEEE80211_ADDR_COPY(wreq.addr.sa_data, ni->ni_macaddr);
		wreq.addr.sa_family = ARPHRD_ETHER;
		wireless_send_event(ic->ic_dev, IWEVREGISTERED, &wreq, NULL);
	}
}

void
ieee80211_notify_node_leave(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	union iwreq_data wreq;

	if (ni == ic->ic_bss) {
		netif_carrier_off(ic->ic_dev);
		memset(wreq.ap_addr.sa_data, 0, ETHER_ADDR_LEN);
		wreq.ap_addr.sa_family = ARPHRD_ETHER;
		wireless_send_event(ic->ic_dev, SIOCGIWAP, &wreq, NULL);
	} else {
		/* fire off wireless event station leaving */
		memset(&wreq, 0, sizeof(wreq));
		IEEE80211_ADDR_COPY(wreq.addr.sa_data, ni->ni_macaddr);
		wreq.addr.sa_family = ARPHRD_ETHER;
		wireless_send_event(ic->ic_dev, IWEVEXPIRED, &wreq, NULL);
	}
}

void
ieee80211_notify_scan_done(struct ieee80211com *ic)
{
	union iwreq_data wreq;

	IEEE80211_DPRINTF(ic, IEEE80211_MSG_SCAN,
		("%s: notify scan done\n", ic->ic_dev->name));

	/* dispatch wireless event indicating scan completed */
	wreq.data.length = 0;
	wreq.data.flags = 0;
	wireless_send_event(ic->ic_dev, SIOCGIWSCAN, &wreq, NULL);
}

void
ieee80211_notify_replay_failure(struct ieee80211com *ic,
	const struct ieee80211_frame *wh, const struct ieee80211_key *k,
	u_int64_t rsc)
{
	static const char * tag = "MLME-REPLAYFAILURE.indication";
	union iwreq_data wrqu;
	char buf[128];

	IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
		("[%s] %s replay detected <rsc %llu, csc %llu>\n",
		ether_sprintf(wh->i_addr2), k->wk_cipher->ic_name,
		rsc, k->wk_keyrsc));

	if (ic->ic_dev == NULL)		/* NB: for cipher test modules */
		return;

	/* TODO: needed parameters: count, keyid, key type, src address, TSC */
	snprintf(buf, sizeof(buf), "%s(keyid=%d %scast addr=%s)", tag,
	    k->wk_keyix, IEEE80211_IS_MULTICAST(wh->i_addr1) ?  "broad" : "uni",
	    ether_sprintf(wh->i_addr1));
	memset(&wrqu, 0, sizeof(wrqu));
	wrqu.data.length = strlen(buf);
	wireless_send_event(ic->ic_dev, IWEVCUSTOM, &wrqu, buf);
}
EXPORT_SYMBOL(ieee80211_notify_replay_failure);

void
ieee80211_notify_michael_failure(struct ieee80211com *ic,
	const struct ieee80211_frame *wh, u_int keyix)
{
	static const char * tag = "MLME-MICHAELMICFAILURE.indication";
	union iwreq_data wrqu;
	char buf[128];

	IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
		("[%s] Michael MIC verification failed <keyidx %d>\n",
	       ether_sprintf(wh->i_addr2), keyix));
	ic->ic_stats.is_rx_tkipmic++;

	if (ic->ic_dev == NULL)		/* NB: for cipher test modules */
		return;

	/* TODO: needed parameters: count, keyid, key type, src address, TSC */
	snprintf(buf, sizeof(buf), "%s(keyid=%d %scast addr=%s)", tag,
	    keyix, IEEE80211_IS_MULTICAST(wh->i_addr1) ?  "broad" : "uni",
	    ether_sprintf(wh->i_addr1));
	memset(&wrqu, 0, sizeof(wrqu));
	wrqu.data.length = strlen(buf);
	wireless_send_event(ic->ic_dev, IWEVCUSTOM, &wrqu, buf);
}
EXPORT_SYMBOL(ieee80211_notify_michael_failure);

#ifdef CONFIG_SYSCTL
#include <linux/ctype.h>

static int
proc_read_node(char *page, int space, struct ieee80211com *ic, void *arg)
{
	TAILQ_HEAD(, ieee80211_node) *head = arg;
	char *p = page;
	struct ieee80211_node *ni;
	struct ieee80211_rateset *rs;
	int i;

	IEEE80211_NODE_LOCK_BH(ic);
	TAILQ_FOREACH(ni, head, ni_list) {
		/* Assume each node needs 300 bytes */ 
		if (p - page > space - 300)
			break;

		p += sprintf(p, "\nmacaddr: <%s>\n", ether_sprintf(ni->ni_macaddr));
		p += sprintf(p, "  rssi: %d dBm\n",
			(*ic->ic_node_getrssi)(ic, ni));

		p += sprintf(p, "  capinfo:");
		if (ni->ni_capinfo & IEEE80211_CAPINFO_ESS)
			p += sprintf(p, " ess");
		if (ni->ni_capinfo & IEEE80211_CAPINFO_IBSS)
			p += sprintf(p, " ibss");
		if (ni->ni_capinfo & IEEE80211_CAPINFO_CF_POLLABLE)
			p += sprintf(p, " cfpollable");
		if (ni->ni_capinfo & IEEE80211_CAPINFO_CF_POLLREQ)
			p += sprintf(p, " cfpollreq");
		if (ni->ni_capinfo & IEEE80211_CAPINFO_PRIVACY)
			p += sprintf(p, " privacy");
		if (ni->ni_capinfo & IEEE80211_CAPINFO_CHNL_AGILITY)
			p += sprintf(p, " chanagility");
		if (ni->ni_capinfo & IEEE80211_CAPINFO_PBCC)
			p += sprintf(p, " pbcc");
		if (ni->ni_capinfo & IEEE80211_CAPINFO_SHORT_PREAMBLE)
			p += sprintf(p, " shortpreamble");
		if (ni->ni_capinfo & IEEE80211_CAPINFO_SHORT_SLOTTIME)
			p += sprintf(p, " shortslot");
		if (ni->ni_capinfo & IEEE80211_CAPINFO_DSSSOFDM)
			p += sprintf(p, " dsssofdm");
		p += sprintf(p, "\n");

		p += sprintf(p, "  freq: %d MHz (channel %d)\n",
			ni->ni_chan->ic_freq,
			ieee80211_mhz2ieee(ni->ni_chan->ic_freq, 0));

		p += sprintf(p, "  opmode:");
		if (IEEE80211_IS_CHAN_A(ni->ni_chan))
			p += sprintf(p, " a");
		if (IEEE80211_IS_CHAN_B(ni->ni_chan))
			p += sprintf(p, " b");
		if (IEEE80211_IS_CHAN_PUREG(ni->ni_chan))
			p += sprintf(p, " pureg");
		if (IEEE80211_IS_CHAN_G(ni->ni_chan))
			p += sprintf(p, " g");
		p += sprintf(p, "\n");

		rs = &ni->ni_rates;
		if (ni->ni_txrate >= 0 && ni->ni_txrate < rs->rs_nrates) {
			p += sprintf(p, "  txrate: ");
			for (i = 0; i < rs->rs_nrates; i++) {
				p += sprintf(p, "%s%d%sMbps",
					(i != 0 ? " " : ""),
					(rs->rs_rates[i] & IEEE80211_RATE_VAL) / 2,
					((rs->rs_rates[i] & 0x1) != 0 ? ".5" : ""));
				if (i == ni->ni_txrate)
					p += sprintf(p, "*"); /* current rate */
			}
			p += sprintf(p, "\n");
		} else
			p += sprintf(p, "  txrate: %d ? (rs_nrates: %d)\n",
					ni->ni_txrate, ni->ni_rates.rs_nrates);

		p += sprintf(p, "  txseq: %d  rxseq: %d\n",
				ni->ni_txseq, ni->ni_rxseq);
		p += sprintf(p, "  fails: %d  inact: %d\n",
				ni->ni_fails, ni->ni_inact);

	}
	IEEE80211_NODE_UNLOCK_BH(ic);
	return (p - page);
}

static int
IEEE80211_SYSCTL_DECL(ieee80211_sysctl_stations, ctl, write, filp, buffer,
		lenp, ppos)
{
	struct ieee80211com *ic = ctl->extra1;
	int len = *lenp;

	if (ic->ic_opmode != IEEE80211_M_HOSTAP &&
	    ic->ic_opmode != IEEE80211_M_IBSS)
		return -EINVAL;
	if (len && filp->f_pos == 0) {
		*lenp = proc_read_node(buffer, len, ic, &ic->ic_node);
		filp->f_pos += *lenp;
	} else {
		*lenp = 0;
	}
	return 0;
}


static int
IEEE80211_SYSCTL_DECL(ieee80211_sysctl_debug, ctl, write, filp, buffer,
		lenp, ppos)
{
	struct ieee80211com *ic = ctl->extra1;
	u_int val;
	int ret;

	ctl->data = &val;
	ctl->maxlen = sizeof(val);
	if (write) {
		ret = IEEE80211_SYSCTL_PROC_DOINTVEC(ctl, write, filp, buffer,
				lenp, ppos);
		if (ret == 0)
			ic->msg_enable = val;
	} else {
		val = ic->msg_enable;
		ret = IEEE80211_SYSCTL_PROC_DOINTVEC(ctl, write, filp, buffer,
				lenp, ppos);
	}
	return ret;
}

#define	CTL_AUTO	-2	/* cannot be CTL_ANY or CTL_NONE */

static const ctl_table ieee80211_sysctl_template[] = {
	{ .ctl_name	= CTL_AUTO,
	  .procname	= "debug",
	  .mode		= 0644,
	  .proc_handler	= ieee80211_sysctl_debug
	},
	{ .ctl_name	= CTL_AUTO,
	  .procname	= "associated_sta",
	  .mode		= 0444,
	  .proc_handler	= ieee80211_sysctl_stations
	},
	{ 0 }
};

void
ieee80211_sysctl_register(struct ieee80211com *ic)
{
	const char *cp;
	int i, space;

	space = 5*sizeof(struct ctl_table) + sizeof(ieee80211_sysctl_template);
	ic->ic_sysctls = kmalloc(space, GFP_KERNEL);
	if (ic->ic_sysctls == NULL) {
		printk("%s: no memory for sysctl table!\n", __func__);
		return;
	}

	/* setup the table */
	memset(ic->ic_sysctls, 0, space);
	ic->ic_sysctls[0].ctl_name = CTL_NET;
	ic->ic_sysctls[0].procname = "net";
	ic->ic_sysctls[0].mode = 0555;
	ic->ic_sysctls[0].child = &ic->ic_sysctls[2];
	/* [1] is NULL terminator */
	ic->ic_sysctls[2].ctl_name = CTL_AUTO;
	for (cp = ic->ic_dev->name; *cp && !isdigit(*cp); cp++)
		;
	snprintf(ic->ic_procname, sizeof(ic->ic_procname), "wlan%s", cp);
	ic->ic_sysctls[2].procname = ic->ic_procname;
	ic->ic_sysctls[2].mode = 0555;
	ic->ic_sysctls[2].child = &ic->ic_sysctls[4];
	/* [3] is NULL terminator */
	/* copy in pre-defined data */
	memcpy(&ic->ic_sysctls[4], ieee80211_sysctl_template,
		sizeof(ieee80211_sysctl_template));

	/* add in dynamic data references */
	for (i = 4; ic->ic_sysctls[i].ctl_name; i++)
		if (ic->ic_sysctls[i].extra1 == NULL)
			ic->ic_sysctls[i].extra1 = ic;

	/* and register everything */
	ic->ic_sysctl_header = register_sysctl_table(ic->ic_sysctls, 1);
	if (!ic->ic_sysctl_header) {
		printk("%s: failed to register sysctls!\n", ic->ic_procname);
		kfree(ic->ic_sysctls);
		ic->ic_sysctls = NULL;
	}
}
EXPORT_SYMBOL(ieee80211_sysctl_register);

void
ieee80211_sysctl_unregister(struct ieee80211com *ic)
{
	if (ic->ic_sysctl_header) {
		unregister_sysctl_table(ic->ic_sysctl_header);
		ic->ic_sysctl_header = NULL;
	}
	if (ic->ic_sysctls) {
		kfree(ic->ic_sysctls);
		ic->ic_sysctls = NULL;
	}
}
EXPORT_SYMBOL(ieee80211_sysctl_unregister);
#endif /* CONFIG_SYSCTL */

/*
 * Format an Ethernet MAC for printing.
 */
const char*
ether_sprintf(const u_int8_t *mac)
{
	static char etherbuf[18];
	snprintf(etherbuf, sizeof(etherbuf), "%02x:%02x:%02x:%02x:%02x:%02x",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return etherbuf;
}
EXPORT_SYMBOL(ether_sprintf);		/* XXX */

/*
 * Module glue.
 */
#include "version.h"
static	char *version = WLAN_VERSION " (EXPERIMENTAL)";
static	char *dev_info = "wlan";

MODULE_AUTHOR("Errno Consulting, Sam Leffler");
MODULE_DESCRIPTION("802.11 wireless LAN protocol support");
#ifdef MODULE_LICENSE
MODULE_LICENSE("Dual BSD/GPL");
#endif

static int __init
init_wlan(void)
{
	printk(KERN_INFO "%s: %s\n", dev_info, version);
	return 0;
}
module_init(init_wlan);

static void __exit
exit_wlan(void)
{
	printk(KERN_INFO "%s: driver unloaded\n", dev_info);
}
module_exit(exit_wlan);
