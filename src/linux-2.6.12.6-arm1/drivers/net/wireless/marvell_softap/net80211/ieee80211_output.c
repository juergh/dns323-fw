/*-
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002-2004 Sam Leffler, Errno Consulting
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
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
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
 * $Id: ieee80211_output.c,v 1.1.1.1 2009/08/19 08:02:21 jack Exp $
 */
#ifndef EXPORT_SYMTAB
#define	EXPORT_SYMTAB
#endif

__FBSDID("$FreeBSD: src/sys/net80211/ieee80211_output.c,v 1.9 2003/10/17 23:15:30 sam Exp $");
__KERNEL_RCSID(0, "$NetBSD: ieee80211_output.c,v 1.9 2003/11/02 00:17:27 dyoung Exp $");

/*
 * IEEE 802.11 output handling.
 */
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>

#include "if_llc.h"
#include "if_ethersubr.h"
#include "if_media.h"

#include <net80211/ieee80211_var.h>

#ifdef IEEE80211_DEBUG
/*
 * Decide if an outbound management frame should be
 * printed when debugging is enabled.  This filters some
 * of the less interesting frames that come frequently
 * (e.g. beacons).
 */
static __inline int
doprint(struct ieee80211com *ic, int subtype)
{
	switch (subtype) {
	case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
		return (ic->ic_opmode == IEEE80211_M_IBSS);
	}
	return 1;
}
#endif

/*
 * Send a management frame to the specified node.  The node pointer
 * must have a reference as the pointer will be passed to the driver
 * and potentially held for a long time.  If the frame is successfully
 * dispatched to the driver, then it is responsible for freeing the
 * reference (and potentially free'ing up any associated storage).
 */
static void
ieee80211_mgmt_output(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct sk_buff *skb, int type)
{
	struct ieee80211_frame *wh;
	struct ieee80211_cb *cb = (struct ieee80211_cb *)skb->cb;

	KASSERT(ni != NULL, ("null node"));
	ni->ni_inact = ic->ic_inact_auth;

	/*
	 * Yech, hack alert!  We want to pass the node down to the
	 * driver's start routine.  If we don't do so then the start
	 * routine must immediately look it up again and that can
	 * cause a lock order reversal if, for example, this frame
	 * is being sent because the station is being timedout and
	 * the frame being sent is a DEAUTH message. 
	 */
	cb->ni = ni;

	wh = (struct ieee80211_frame *)
		skb_push(skb, sizeof(struct ieee80211_frame));
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_MGT | type;
	wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	*(u_int16_t *)&wh->i_dur[0] = 0;
	*(u_int16_t *)&wh->i_seq[0] =
	    htole16(ni->ni_txseq << IEEE80211_SEQ_SEQ_SHIFT);
	ni->ni_txseq++;
	IEEE80211_ADDR_COPY(wh->i_addr1, ni->ni_macaddr);
	IEEE80211_ADDR_COPY(wh->i_addr2, ic->ic_myaddr);
	IEEE80211_ADDR_COPY(wh->i_addr3, ni->ni_bssid);

	if ((cb->flags & M_LINK0) != 0 && ni->ni_challenge != NULL) {
		cb->flags &= ~M_LINK0;
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_AUTH,
			("%s: encrypting frame for %s\n",
			__func__, ether_sprintf(wh->i_addr1)));
		wh->i_fc[1] |= IEEE80211_FC1_WEP;
	}
#ifdef IEEE80211_DEBUG
	/* avoid printing too many frames */
	if ((ieee80211_msg_debug(ic) && doprint(ic, type)) ||
	    ieee80211_msg_dumppkts(ic)) {
		if_printf(ic->ic_dev, "sending %s to %s on channel %u\n",
		    ieee80211_mgt_subtype_name[
		    (type & IEEE80211_FC0_SUBTYPE_MASK)
		    >> IEEE80211_FC0_SUBTYPE_SHIFT],
		    ether_sprintf(ni->ni_macaddr),
		    ieee80211_chan2ieee(ic, ni->ni_chan));
	}
#endif
	(void) (*ic->ic_mgtstart)(ic, skb);
}

/*
 * Insure there is sufficient headroom and tailroom to
 * encapsulate the 802.11 data frame.  If room isn't
 * already there, reallocate so there is enough space.
 * Drivers and cipher modules assume we have done the
 * necessary work and fail rudely if they don't find
 * the space they need.
 */
static struct sk_buff *
ieee80211_skbhdr_adjust(struct ieee80211com *ic, struct ieee80211_key *key,
	struct sk_buff *skb)
{
	/* XXX too conservative, e.g. qos frame */
	/* XXX pre-calculate per node? */
	struct sk_buff *newskb = NULL;
	int need_headroom = sizeof(struct ieee80211_qosframe)
		 + sizeof(struct llc)
		 + IEEE80211_ADDR_LEN
		 ;
	int need_tailroom = 0;

	if (key != NULL) {
		const struct ieee80211_cipher *cip = key->wk_cipher;
		/*
		 * Adjust for crypto needs.  When hardware crypto is
		 * being used we assume the hardware/driver will deal
		 * with any padding (on the fly, without needing to
		 * expand the frame contents).  When software crypto
		 * is used we need to insure room is available at the
		 * front and back and also for any per-MSDU additions.
		 */
		/* XXX belongs in crypto code? */
		need_headroom += cip->ic_header;
		/* XXX pre-calculate per key */
		if (key->wk_flags & IEEE80211_KEY_SWCRYPT)
			need_tailroom += cip->ic_trailer;
		/* XXX frags */
		if (key->wk_flags & IEEE80211_KEY_SWMIC)
			need_tailroom += cip->ic_miclen;
	}

	skb = skb_unshare(skb, GFP_ATOMIC);
	if (skb == NULL) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_OUTPUT,
		    ("%s: cannot unshare for encapsulation\n", __func__));
		ic->ic_stats.is_tx_nobuf++;
	} else if (skb_tailroom(skb) < need_tailroom) {
#if 1
		/* RR older Kernels have a problem using pskb_expand_head() */
		/* (lead to memory problems under stress) */
		newskb = alloc_skb(ic->ic_dev->mtu + 128, GFP_ATOMIC);
		if (newskb != NULL) {
			skb_reserve(newskb, 64); /* 64 byte headroom */
			memcpy(newskb->data, skb->data, skb->len);
			skb_put(newskb, skb->len);
			newskb->dev = skb->dev;
			newskb->protocol = skb->protocol;
		} 
		dev_kfree_skb(skb);
		skb = newskb;
#else
		if (pskb_expand_head(skb, need_headroom - skb_headroom(skb),
			need_tailroom - skb_tailroom(skb), GFP_ATOMIC)) {
			dev_kfree_skb(skb);
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_OUTPUT,
			    ("%s: cannot expand storage (tail)\n", __func__));
			ic->ic_stats.is_tx_nobuf++;
			skb = NULL;
		}
#endif
	} else if (skb_headroom(skb) < need_headroom) {
		struct sk_buff *tmp = skb;
		skb = skb_realloc_headroom(skb, need_headroom);
		dev_kfree_skb(tmp);
		if (skb == NULL) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_OUTPUT,
			    ("%s: cannot expand storage (head)\n", __func__));
			ic->ic_stats.is_tx_nobuf++;
		}
	}
	return skb;
}

/*
 * Return the transmit key to use in sending a frame to
 * the specified destination. Multicast traffic always
 * uses the group key.  Otherwise if a unicast key is
 * set we use that.  When no unicast key is set we fall
 * back to the default transmit key.
 */ 
static inline struct ieee80211_key *
ieee80211_crypto_getkey(struct ieee80211com *ic,
	const u_int8_t mac[IEEE80211_ADDR_LEN], struct ieee80211_node *ni)
{
#define	KEY_UNDEFINED(k)	((k).wk_cipher == &ieee80211_cipher_none)
	if (IEEE80211_IS_MULTICAST(mac) || KEY_UNDEFINED(ni->ni_ucastkey)) {
		if (ic->ic_def_txkey == IEEE80211_KEYIX_NONE ||
		    KEY_UNDEFINED(ic->ic_nw_keys[ic->ic_def_txkey])) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_OUTPUT,
			    ("%s: no transmit key, def_txkey %u\n",
			    __func__, ic->ic_def_txkey));
			/* XXX statistic */
			return NULL;
		}
		return &ic->ic_nw_keys[ic->ic_def_txkey];
	} else {
		return &ni->ni_ucastkey;
	}
#undef KEY_UNDEFINED
}

/*
 * Encapsulate an outbound data frame.  The mbuf chain is updated and
 * a reference to the destination node is returned.  If an error is
 * encountered NULL is returned and the node reference will also be NULL.
 * 
 * NB: The caller is responsible for free'ing a returned node reference.
 *     The convention is ic_bss is not reference counted; the caller must
 *     maintain that.
 */
struct sk_buff *
ieee80211_encap(struct ieee80211com *ic, struct sk_buff *skb,
	struct ieee80211_node **pni)
{
	struct ether_header eh;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	struct ieee80211_key *key;
	struct llc *llc;

	memcpy(&eh, skb->data, sizeof(struct ether_header));
	skb_pull(skb, sizeof(struct ether_header));

	ni = ieee80211_find_txnode(ic, eh.ether_dhost);
	if (ni == NULL) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_OUTPUT,
			("%s: no node for dst %s, discard frame\n",
			__func__, ether_sprintf(eh.ether_dhost)));
		ic->ic_stats.is_tx_nonode++; 
		goto bad;
	}

	/* 
	 * If node has a vlan tag then all traffic
	 * to it must have a matching tag.
	 */
	if (ni->ni_vlan != 0) {
		if (ic->ic_vlgrp == NULL || !vlan_tx_tag_present(skb)) {
			ni->ni_stats.ns_tx_novlantag++;
			goto bad;
		}
		if (vlan_tx_tag_get(skb) != ni->ni_vlan) {
			ni->ni_stats.ns_tx_vlanmismatch++;
			goto bad;
		}
	}

	/*
	 * Insure space for additional headers.  First
	 * identify transmit key to use in calculating any
	 * buffer adjustments required.  This is also used
	 * below to do privacy encapsulation work.
	 *
	 * Note key may be NULL if we fall back to the default
	 * transmit key and that is not set.  In that case the
	 * buffer may not be expanded as needed by the cipher
	 * routines, but they will/should discard it.
	 */
	if (ic->ic_flags & IEEE80211_F_PRIVACY)
		key = ieee80211_crypto_getkey(ic, eh.ether_dhost, ni);
	else
		key = NULL;
	skb = ieee80211_skbhdr_adjust(ic, key, skb);
	if (skb == NULL) {
		/* NB: ieee80211_skbhdr_adjust handles msgs+statistics */
		goto bad;
	}

	// return skb;  **** data XMIT works fine up to this point...
	llc = (struct llc *) skb_push(skb, sizeof(struct llc));
	// return skb;  // **** mark point
	llc->llc_dsap = llc->llc_ssap = LLC_SNAP_LSAP;
	llc->llc_control = LLC_UI;
	llc->llc_snap.org_code[0] = 0;
	llc->llc_snap.org_code[1] = 0;
	llc->llc_snap.org_code[2] = 0;
	llc->llc_snap.ether_type = eh.ether_type;
	// return skb;  // **** mark point

	wh = (struct ieee80211_frame *)
		skb_push(skb, sizeof(struct ieee80211_frame));;
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_DATA;
	*(u_int16_t *)&wh->i_dur[0] = 0;
	*(u_int16_t *)&wh->i_seq[0] =
	    htole16(ni->ni_txseq << IEEE80211_SEQ_SEQ_SHIFT);
	ni->ni_txseq++;
	// return skb;  // **** mark point
	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
		wh->i_fc[1] = IEEE80211_FC1_DIR_TODS;
		IEEE80211_ADDR_COPY(wh->i_addr1, ni->ni_bssid);
		IEEE80211_ADDR_COPY(wh->i_addr2, eh.ether_shost);
		IEEE80211_ADDR_COPY(wh->i_addr3, eh.ether_dhost);
		break;
	case IEEE80211_M_IBSS:
	case IEEE80211_M_AHDEMO:
		wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
		IEEE80211_ADDR_COPY(wh->i_addr1, eh.ether_dhost);
		IEEE80211_ADDR_COPY(wh->i_addr2, eh.ether_shost);
		IEEE80211_ADDR_COPY(wh->i_addr3, ni->ni_bssid);
		break;
	case IEEE80211_M_HOSTAP:
		wh->i_fc[1] = IEEE80211_FC1_DIR_FROMDS;
		IEEE80211_ADDR_COPY(wh->i_addr1, eh.ether_dhost);
		IEEE80211_ADDR_COPY(wh->i_addr2, ni->ni_bssid);
		IEEE80211_ADDR_COPY(wh->i_addr3, eh.ether_shost);
		break;
	case IEEE80211_M_MONITOR:
		goto bad;
	}
	// return skb; *** at least at this point a freeze was discovered
	if (eh.ether_type != __constant_htons(ETHERTYPE_PAE) ||
	    (key != NULL && (ic->ic_flags & IEEE80211_F_WPA))) {
		/*
		 * IEEE 802.1X: send EAPOL frames always in the clear.
		 * WPA/WPA2: encrypt EAPOL keys when pairwise keys are set.
		 */
		if (key != NULL) {
			wh->i_fc[1] |= IEEE80211_FC1_WEP;
			/* XXX do fragmentation */
			if (!ieee80211_crypto_enmic(ic, key, skb)) {
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
				    ("[%s] enmic failed, discard frame\n",
				    ether_sprintf(eh.ether_dhost)));
				/* XXX statistic */
				goto bad;
			}
		}
	}
	if (eh.ether_type != __constant_htons(ETHERTYPE_PAE)) {
		/*
		 * Reset the inactivity timer only for non-PAE traffic
		 * to avoid a problem where the station leaves w/o
		 * notice while we're requesting Identity.  In this
		 * situation the 802.1x state machine will continue
		 * to retransmit the requests because it assumes the
		 * station will be timed out for inactivity, but our
		 * retransmits will reset the inactivity timer.
		 */ 
		ni->ni_inact = ic->ic_inact_run;
	}
	*pni = ni;
	return skb;
bad:
	if (skb != NULL)
		dev_kfree_skb(skb);
	if (ni && ni != ic->ic_bss)
		ieee80211_free_node(ic, ni);
	*pni = NULL;
	return NULL;
}
EXPORT_SYMBOL(ieee80211_encap);

/*
 * Add a supported rates element id to a frame.
 */
static u_int8_t *
ieee80211_add_rates(u_int8_t *frm, const struct ieee80211_rateset *rs)
{
	int nrates;

	*frm++ = IEEE80211_ELEMID_RATES;
	nrates = rs->rs_nrates;
	if (nrates > IEEE80211_RATE_SIZE)
		nrates = (IEEE80211_RATE_SIZE/2);
	*frm++ = nrates;
	memcpy(frm, rs->rs_rates, nrates);
	return frm + nrates;
}

/*
 * Add an extended supported rates element id to a frame.
 */
static u_int8_t *
ieee80211_add_xrates(u_int8_t *frm, const struct ieee80211_rateset *rs)
{
	/*
	 * Add an extended supported rates element if operating in 11g mode.
	 */
	if (rs->rs_nrates > IEEE80211_RATE_SIZE) {
		int nrates = IEEE80211_RATE_SIZE; // rs->rs_nrates-IEEE80211_RATE_SIZE;
		*frm++ = IEEE80211_ELEMID_XRATES;
		*frm++ = nrates;
		memcpy(frm, rs->rs_rates + (IEEE80211_RATE_SIZE/2), nrates);
		frm += nrates;
	}
	return frm;
}

/* 
 * Add an ssid elemet to a frame.
 */
static u_int8_t *
ieee80211_add_ssid(u_int8_t *frm, const u_int8_t *ssid, u_int len)
{
	*frm++ = IEEE80211_ELEMID_SSID;
	*frm++ = len;
	memcpy(frm, ssid, len);
	return frm + len;
}

/*
 * Add an erp element to a frame.
 */
static u_int8_t *
ieee80211_add_erp(u_int8_t *frm, struct ieee80211com *ic)
{
	u_int8_t erp;

	*frm++ = IEEE80211_ELEMID_ERP;
	*frm++ = 1;
	erp = 0;
	if (ic->ic_nonerpsta != 0)
		erp |= IEEE80211_ERP_NON_ERP_PRESENT;
	if (ic->ic_flags & IEEE80211_F_USEPROT)
		erp |= IEEE80211_ERP_USE_PROTECTION;
	if (ic->ic_flags & IEEE80211_F_USEBARKER)
		erp |= IEEE80211_ERP_LONG_PREAMBLE;
	*frm++ = erp;
	return frm;
}

static u_int8_t *
ieee80211_setup_wpa_ie(struct ieee80211com *ic, u_int8_t *ie)
{
#define	WPA_OUI_BYTES		0x00, 0x50, 0xf2
#define	ADDSHORT(frm, v) do {			\
	frm[0] = (v) & 0xff;			\
	frm[1] = (v) >> 8;			\
	frm += 2;				\
} while (0)
#define	ADDSELECTOR(frm, sel) do {		\
	memcpy(frm, sel, 4);			\
	frm += 4;				\
} while (0)
	static const u_int8_t oui[4] = { WPA_OUI_BYTES, WPA_OUI_TYPE };
	static const u_int8_t cipher_suite[][4] = {
		{ WPA_OUI_BYTES, WPA_CSE_WEP40 },	/* NB: 40-bit */
		{ WPA_OUI_BYTES, WPA_CSE_TKIP },
		{ 0x00, 0x00, 0x00, 0x00 },		/* XXX WRAP */
		{ WPA_OUI_BYTES, WPA_CSE_CCMP },
		{ 0x00, 0x00, 0x00, 0x00 },		/* XXX CKIP */
		{ WPA_OUI_BYTES, WPA_CSE_NULL },
	};
	static const u_int8_t wep104_suite[4] =
		{ WPA_OUI_BYTES, WPA_CSE_WEP104 };
	static const u_int8_t key_mgt_unspec[4] =
		{ WPA_OUI_BYTES, WPA_ASE_8021X_UNSPEC };
	static const u_int8_t key_mgt_psk[4] =
		{ WPA_OUI_BYTES, WPA_ASE_8021X_PSK };
	const struct ieee80211_rsnparms *rsn = &ic->ic_bss->ni_rsn;
	u_int8_t *frm = ie;
	u_int8_t *selcnt;

	*frm++ = IEEE80211_ELEMID_VENDOR;
	*frm++ = 0;				/* length filled in below */
	memcpy(frm, oui, sizeof(oui));		/* WPA OUI */
	frm += sizeof(oui);
	ADDSHORT(frm, WPA_VERSION);

	/* XXX filter out CKIP */

	/* multicast cipher */
	if (rsn->rsn_mcastcipher == IEEE80211_CIPHER_WEP &&
	    rsn->rsn_mcastkeylen >= 13)
		ADDSELECTOR(frm, wep104_suite);
	else
		ADDSELECTOR(frm, cipher_suite[rsn->rsn_mcastcipher]);

	/* unicast cipher list */
	selcnt = frm;
	ADDSHORT(frm, 0);			/* selector count */
	if (rsn->rsn_ucastcipherset & (1<<IEEE80211_CIPHER_TKIP)) {
		selcnt[0]++;
		ADDSELECTOR(frm, cipher_suite[IEEE80211_CIPHER_TKIP]);
	}
	if (rsn->rsn_ucastcipherset & (1<<IEEE80211_CIPHER_AES_CCM)) {
		selcnt[0]++;
		ADDSELECTOR(frm, cipher_suite[IEEE80211_CIPHER_AES_CCM]);
	}

	/* authenticator selector list */
	selcnt = frm;
	ADDSHORT(frm, 0);			/* selector count */
	if (rsn->rsn_keymgmtset & WPA_ASE_8021X_UNSPEC) {
		selcnt[0]++;
		ADDSELECTOR(frm, key_mgt_unspec);
	}
	if (rsn->rsn_keymgmtset & WPA_ASE_8021X_PSK) {
		selcnt[0]++;
		ADDSELECTOR(frm, key_mgt_psk);
	}

	/* optional capabilities */
	if (rsn->rsn_caps != 0)
		ADDSHORT(frm, rsn->rsn_caps);

	/* calculate element length */
	ie[1] = frm - ie - 2;
	KASSERT(ie[1]+2 <= sizeof(struct ieee80211_ie_wpa),
		("WPA IE too big, %u > %u",
		ie[1]+2, sizeof(struct ieee80211_ie_wpa)));
	return frm;
#undef ADDSHORT
#undef ADDSELECTOR
#undef WPA_OUI_BYTES
}

static u_int8_t *
ieee80211_setup_rsn_ie(struct ieee80211com *ic, u_int8_t *ie)
{
#define	RSN_OUI_BYTES		0x00, 0x0f, 0xac
#define	ADDSHORT(frm, v) do {			\
	frm[0] = (v) & 0xff;			\
	frm[1] = (v) >> 8;			\
	frm += 2;				\
} while (0)
#define	ADDSELECTOR(frm, sel) do {		\
	memcpy(frm, sel, 4);			\
	frm += 4;				\
} while (0)
	static const u_int8_t cipher_suite[][4] = {
		{ RSN_OUI_BYTES, RSN_CSE_WEP40 },	/* NB: 40-bit */
		{ RSN_OUI_BYTES, RSN_CSE_TKIP },
		{ RSN_OUI_BYTES, RSN_CSE_WRAP },
		{ RSN_OUI_BYTES, RSN_CSE_CCMP },
		{ 0x00, 0x00, 0x00, 0x00 },		/* XXX CKIP */
		{ RSN_OUI_BYTES, RSN_CSE_NULL },
	};
	static const u_int8_t wep104_suite[4] =
		{ RSN_OUI_BYTES, RSN_CSE_WEP104 };
	static const u_int8_t key_mgt_unspec[4] =
		{ RSN_OUI_BYTES, RSN_ASE_8021X_UNSPEC };
	static const u_int8_t key_mgt_psk[4] =
		{ RSN_OUI_BYTES, RSN_ASE_8021X_PSK };
	const struct ieee80211_rsnparms *rsn = &ic->ic_bss->ni_rsn;
	u_int8_t *frm = ie;
	u_int8_t *selcnt;

	*frm++ = IEEE80211_ELEMID_RSN;
	*frm++ = 0;				/* length filled in below */
	ADDSHORT(frm, RSN_VERSION);

	/* XXX filter out CKIP */

	/* multicast cipher */
	if (rsn->rsn_mcastcipher == IEEE80211_CIPHER_WEP &&
	    rsn->rsn_mcastkeylen >= 13)
		ADDSELECTOR(frm, wep104_suite);
	else
		ADDSELECTOR(frm, cipher_suite[rsn->rsn_mcastcipher]);

	/* unicast cipher list */
	selcnt = frm;
	ADDSHORT(frm, 0);			/* selector count */
	if (rsn->rsn_ucastcipherset & (1<<IEEE80211_CIPHER_TKIP)) {
		selcnt[0]++;
		ADDSELECTOR(frm, cipher_suite[IEEE80211_CIPHER_TKIP]);
	}
	if (rsn->rsn_ucastcipherset & (1<<IEEE80211_CIPHER_AES_CCM)) {
		selcnt[0]++;
		ADDSELECTOR(frm, cipher_suite[IEEE80211_CIPHER_AES_CCM]);
	}

	/* authenticator selector list */
	selcnt = frm;
	ADDSHORT(frm, 0);			/* selector count */
	if (rsn->rsn_keymgmtset & WPA_ASE_8021X_UNSPEC) {
		selcnt[0]++;
		ADDSELECTOR(frm, key_mgt_unspec);
	}
	if (rsn->rsn_keymgmtset & WPA_ASE_8021X_PSK) {
		selcnt[0]++;
		ADDSELECTOR(frm, key_mgt_psk);
	}

	/* optional capabilities */
	if (rsn->rsn_caps != 0)
		ADDSHORT(frm, rsn->rsn_caps);
	/* XXX PMKID */

	/* calculate element length */
	ie[1] = frm - ie - 2;
	KASSERT(ie[1]+2 <= sizeof(struct ieee80211_ie_wpa),
		("RSN IE too big, %u > %u",
		ie[1]+2, sizeof(struct ieee80211_ie_wpa)));
	return frm;
#undef ADDSELECTOR
#undef ADDSHORT
#undef RSN_OUI_BYTES
}

/*
 * Add a WPA/RSN element to a frame.
 */
static u_int8_t *
ieee80211_add_wpa(u_int8_t *frm, struct ieee80211com *ic)
{

	KASSERT(ic->ic_flags & IEEE80211_F_WPA, ("no WPA/RSN!"));
	if (ic->ic_flags & IEEE80211_F_WPA1)
		frm = ieee80211_setup_wpa_ie(ic, frm);
	if (ic->ic_flags & IEEE80211_F_WPA2)
		frm = ieee80211_setup_rsn_ie(ic, frm);
	return frm;
}

/*
 * Send a management frame.  The node is for the destination (or ic_bss
 * when in station mode).  Nodes other than ic_bss have their reference
 * count bumped to reflect our use for an indeterminant time.
 */
int
ieee80211_send_mgmt(struct ieee80211com *ic, struct ieee80211_node *ni,
	int type, int arg)
{
#define	senderr(_x, _v)	do { ic->ic_stats._v++; ret = _x; goto bad; } while (0)
	struct sk_buff *skb;
	u_int8_t *frm;
	enum ieee80211_phymode mode;
	u_int16_t capinfo;
	int has_challenge, is_shared_key, ret, timer, status;

	KASSERT(ni != NULL, ("null node"));

	/*
	 * Hold a reference on the node so it doesn't go away until after
	 * the xmit is complete all the way in the driver.  On error we
	 * will remove our reference.
	 */
	if (ni != ic->ic_bss)
		ieee80211_ref_node(ni);
	timer = 0;
	switch (type) {
	case IEEE80211_FC0_SUBTYPE_PROBE_REQ:
		/*
		 * prreq frame format
		 *	[tlv] ssid
		 *	[tlv] supported rates
		 *	[tlv] extended supported rates
		 *	[tlv] user-specified ie's
		 */
		skb = ieee80211_getmgtframe(&frm,
			 2 + ic->ic_des_esslen
		       + 2 + IEEE80211_RATE_SIZE
		       + 2 + (IEEE80211_RATE_MAXSIZE - IEEE80211_RATE_SIZE)
		       + (ic->ic_opt_ie != NULL ? ic->ic_opt_ie_len : 0));
		if (skb == NULL)
			senderr(ENOMEM, is_tx_nobuf);

		frm = ieee80211_add_ssid(frm, ic->ic_des_essid, ic->ic_des_esslen);
		mode = ieee80211_chan2mode(ic, ni->ni_chan);
		frm = ieee80211_add_rates(frm, &ic->ic_sup_rates[mode]);
		frm = ieee80211_add_xrates(frm, &ic->ic_sup_rates[mode]);
		if (ic->ic_opt_ie != NULL) {
			memcpy(frm, ic->ic_opt_ie, ic->ic_opt_ie_len);
			frm += ic->ic_opt_ie_len;
		}
		skb_trim(skb, frm - skb->data);

		timer = IEEE80211_TRANS_WAIT;
		break;

	case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
		/*
		 * probe response frame format
		 *	[8] time stamp
		 *	[2] beacon interval
		 *	[2] cabability information
		 *	[tlv] ssid
		 *	[tlv] supported rates
		 *	[tlv] parameter set (FH/DS)
		 *	[tlv] parameter set (IBSS)
		 *	[tlv] extended rate phy (ERP)
		 *	[tlv] extended supported rates
		 *	[tlv] WPA
		 */
		skb = ieee80211_getmgtframe(&frm,
			 8 + 2 + 2 + 2
		       + 2 + ni->ni_esslen
		       + 2 + IEEE80211_RATE_SIZE
		       + (ic->ic_phytype == IEEE80211_T_FH ? 7 : 3)
		       + 6
		       + (ic->ic_curmode == IEEE80211_MODE_11G ? 3 : 0)
		       + 2 + (IEEE80211_RATE_MAXSIZE - IEEE80211_RATE_SIZE)
		       + (ic->ic_flags & IEEE80211_F_WPA ?
				2*sizeof(struct ieee80211_ie_wpa) : 0)
		);
		if (skb == NULL)
			senderr(ENOMEM, is_tx_nobuf);

		memset(frm, 0, 8);	/* timestamp should be filled later */
		frm += 8;
		*(u_int16_t *)frm = htole16(ic->ic_bss->ni_intval);
		frm += 2;
		if (ic->ic_opmode == IEEE80211_M_IBSS)
			capinfo = IEEE80211_CAPINFO_IBSS;
		else
			capinfo = IEEE80211_CAPINFO_ESS;
		if (ic->ic_flags & IEEE80211_F_PRIVACY)
			capinfo |= IEEE80211_CAPINFO_PRIVACY;
		if ((ic->ic_flags & IEEE80211_F_SHPREAMBLE) &&
		    IEEE80211_IS_CHAN_2GHZ(ni->ni_chan))
			capinfo |= IEEE80211_CAPINFO_SHORT_PREAMBLE;
		if (ic->ic_flags & IEEE80211_F_SHSLOT)
			capinfo |= IEEE80211_CAPINFO_SHORT_SLOTTIME;
		*(u_int16_t *)frm = htole16(capinfo);
		frm += 2;

		frm = ieee80211_add_ssid(frm, ic->ic_bss->ni_essid,
				ic->ic_bss->ni_esslen);
		frm = ieee80211_add_rates(frm, &ic->ic_bss->ni_rates);

		if (ic->ic_phytype == IEEE80211_T_FH) {
                        *frm++ = IEEE80211_ELEMID_FHPARMS;
                        *frm++ = 5;
                        *frm++ = ni->ni_fhdwell & 0x00ff;
                        *frm++ = (ni->ni_fhdwell >> 8) & 0x00ff;
                        *frm++ = IEEE80211_FH_CHANSET(
			    ieee80211_chan2ieee(ic, ni->ni_chan));
                        *frm++ = IEEE80211_FH_CHANPAT(
			    ieee80211_chan2ieee(ic, ni->ni_chan));
                        *frm++ = ni->ni_fhindex;
		} else {
			*frm++ = IEEE80211_ELEMID_DSPARMS;
			*frm++ = 1;
			*frm++ = ieee80211_chan2ieee(ic, ni->ni_chan);
		}

		if (ic->ic_opmode == IEEE80211_M_IBSS) {
			*frm++ = IEEE80211_ELEMID_IBSSPARMS;
			*frm++ = 2;
			*frm++ = 0; *frm++ = 0;		/* TODO: ATIM window */
		}
		if (ic->ic_flags & IEEE80211_F_WPA)
			frm = ieee80211_add_wpa(frm, ic);
		if (ic->ic_curmode == IEEE80211_MODE_11G)
			frm = ieee80211_add_erp(frm, ic);
		frm = ieee80211_add_xrates(frm, &ic->ic_bss->ni_rates);
		skb_trim(skb, frm - skb->data);
		break;

	case IEEE80211_FC0_SUBTYPE_AUTH:
		status = arg >> 16;
		arg &= 0xffff;
		has_challenge = ((arg == IEEE80211_AUTH_SHARED_CHALLENGE ||
		    arg == IEEE80211_AUTH_SHARED_RESPONSE) &&
		    ni->ni_challenge != NULL);

		/*
		 * Deduce whether we're doing open authentication or
		 * shared key authentication.  We do the latter if
		 * we're in the middle of a shared key authentication
		 * handshake or if we're initiating an authentication
		 * request and configured to use shared key.
		 */
		is_shared_key = has_challenge ||
		     arg >= IEEE80211_AUTH_SHARED_RESPONSE ||
		     (arg == IEEE80211_AUTH_SHARED_REQUEST &&
		      ic->ic_bss->ni_authmode == IEEE80211_AUTH_SHARED);

		skb = ieee80211_getmgtframe(&frm,
			  3 * sizeof(u_int16_t)
			+ (has_challenge && status == IEEE80211_STATUS_SUCCESS ?
				sizeof(u_int16_t)+IEEE80211_CHALLENGE_LEN : 0));
		if (skb == NULL)
			senderr(ENOMEM, is_tx_nobuf);

		((u_int16_t *)frm)[0] =
		    (is_shared_key) ? htole16(IEEE80211_AUTH_ALG_SHARED)
		                    : htole16(IEEE80211_AUTH_ALG_OPEN);
		((u_int16_t *)frm)[1] = htole16(arg);	/* sequence number */
		((u_int16_t *)frm)[2] = htole16(status);/* status */

		if (has_challenge && status == IEEE80211_STATUS_SUCCESS) {
			((u_int16_t *)frm)[3] =
			    htole16((IEEE80211_CHALLENGE_LEN << 8) |
			    IEEE80211_ELEMID_CHALLENGE);
			memcpy(&((u_int16_t *)frm)[4], ni->ni_challenge,
			    IEEE80211_CHALLENGE_LEN);
			if (arg == IEEE80211_AUTH_SHARED_RESPONSE) {
				struct ieee80211_cb *cb =
					(struct ieee80211_cb *)skb->cb;
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_AUTH,
				    ("%s: request encrypt frame\n", __func__));
				cb->flags |= M_LINK0; /* WEP-encrypt, please */
			}
		}
		/*
		 * When 802.1x is not in use mark the port
		 * authorized at this point so traffic can flow.
		 * XXX shared key auth
		 */
		if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
		    status == IEEE80211_STATUS_SUCCESS &&
		    ni->ni_authmode != IEEE80211_AUTH_8021X)
			ieee80211_node_authorize(ic, ni);
		if (ic->ic_opmode == IEEE80211_M_STA)
			timer = IEEE80211_TRANS_WAIT;
		break;

	case IEEE80211_FC0_SUBTYPE_DEAUTH:
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_AUTH,
			("send station %s deauthenticate (reason %d)\n",
			ether_sprintf(ni->ni_macaddr), arg));
		skb = ieee80211_getmgtframe(&frm, sizeof(u_int16_t));
		if (skb == NULL)
			senderr(ENOMEM, is_tx_nobuf);
		*(u_int16_t *)frm = htole16(arg);	/* reason */
		ieee80211_node_unauthorize(ic, ni);	/* port closed */
		break;

	case IEEE80211_FC0_SUBTYPE_ASSOC_REQ:
	case IEEE80211_FC0_SUBTYPE_REASSOC_REQ:
		/*
		 * asreq frame format
		 *	[2] capability information
		 *	[2] listen interval
		 *	[6*] current AP address (reassoc only)
		 *	[tlv] ssid
		 *	[tlv] supported rates
		 *	[tlv] extended supported rates
		 *	[tlv] user-specified ie's
		 */
		skb = ieee80211_getmgtframe(&frm,
			 sizeof(capinfo)
		       + sizeof(u_int16_t)
		       + IEEE80211_ADDR_LEN
		       + 2 + ni->ni_esslen
		       + 2 + IEEE80211_RATE_SIZE
		       + 2 + (IEEE80211_RATE_MAXSIZE - IEEE80211_RATE_SIZE)
		       + (ic->ic_opt_ie != NULL ? ic->ic_opt_ie_len : 0));
		if (skb == NULL)
			senderr(ENOMEM, is_tx_nobuf);

		capinfo = 0;
		if (ic->ic_opmode == IEEE80211_M_IBSS)
			capinfo |= IEEE80211_CAPINFO_IBSS;
		else		/* IEEE80211_M_STA */
			capinfo |= IEEE80211_CAPINFO_ESS;
		if (ic->ic_flags & IEEE80211_F_PRIVACY)
			capinfo |= IEEE80211_CAPINFO_PRIVACY;
		/*
		 * NB: Some 11a AP's reject the request when
		 *     short premable is set.
		 */
		if ((ic->ic_flags & IEEE80211_F_SHPREAMBLE) &&
		    IEEE80211_IS_CHAN_2GHZ(ni->ni_chan))
			capinfo |= IEEE80211_CAPINFO_SHORT_PREAMBLE;
		if ((ni->ni_capinfo & IEEE80211_CAPINFO_SHORT_SLOTTIME) &&
		    (ic->ic_caps & IEEE80211_C_SHSLOT))
			capinfo |= IEEE80211_CAPINFO_SHORT_SLOTTIME;
		*(u_int16_t *)frm = htole16(capinfo);
		frm += 2;

		*(u_int16_t *)frm = htole16(ic->ic_lintval);
		frm += 2;

		if (type == IEEE80211_FC0_SUBTYPE_REASSOC_REQ) {
			IEEE80211_ADDR_COPY(frm, ic->ic_bss->ni_bssid);
			frm += IEEE80211_ADDR_LEN;
		}

		frm = ieee80211_add_ssid(frm, ni->ni_essid, ni->ni_esslen);
		frm = ieee80211_add_rates(frm, &ni->ni_rates);
		frm = ieee80211_add_xrates(frm, &ni->ni_rates);
		if (ic->ic_opt_ie != NULL) {
			memcpy(frm, ic->ic_opt_ie, ic->ic_opt_ie_len);
			frm += ic->ic_opt_ie_len;
		}
		skb_trim(skb, frm - skb->data);

		timer = IEEE80211_TRANS_WAIT;
		break;

	case IEEE80211_FC0_SUBTYPE_ASSOC_RESP:
	case IEEE80211_FC0_SUBTYPE_REASSOC_RESP:
		/*
		 * asreq frame format
		 *	[2] capability information
		 *	[2] status
		 *	[2] association ID
		 *	[tlv] supported rates
		 *	[tlv] extended supported rates
		 */
		skb = ieee80211_getmgtframe(&frm,
			 sizeof(capinfo)
		       + sizeof(u_int16_t)
		       + sizeof(u_int16_t)
		       + 2 + IEEE80211_RATE_SIZE
		       + 2 + (IEEE80211_RATE_MAXSIZE - IEEE80211_RATE_SIZE));
		if (skb == NULL)
			senderr(ENOMEM, is_tx_nobuf);

		capinfo = IEEE80211_CAPINFO_ESS;
		if (ic->ic_flags & IEEE80211_F_PRIVACY)
			capinfo |= IEEE80211_CAPINFO_PRIVACY;
		if ((ic->ic_flags & IEEE80211_F_SHPREAMBLE) &&
		    IEEE80211_IS_CHAN_2GHZ(ni->ni_chan))
			capinfo |= IEEE80211_CAPINFO_SHORT_PREAMBLE;
		if (ic->ic_flags & IEEE80211_F_SHSLOT)
			capinfo |= IEEE80211_CAPINFO_SHORT_SLOTTIME;
		*(u_int16_t *)frm = htole16(capinfo);
		frm += 2;

		*(u_int16_t *)frm = htole16(arg);	/* status */
		frm += 2;

		if (arg == IEEE80211_STATUS_SUCCESS)
			*(u_int16_t *)frm = htole16(ni->ni_associd);
		frm += 2;

		frm = ieee80211_add_rates(frm, &ni->ni_rates);
		frm = ieee80211_add_xrates(frm, &ni->ni_rates);
		skb_trim(skb, frm - skb->data);
		break;

	case IEEE80211_FC0_SUBTYPE_DISASSOC:
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ASSOC,
			("send station %s disassociate (reason %d)\n",
			ether_sprintf(ni->ni_macaddr), arg));
		skb = ieee80211_getmgtframe(&frm, sizeof(u_int16_t));
		if (skb == NULL)
			senderr(ENOMEM, is_tx_nobuf);
		*(u_int16_t *)frm = htole16(arg);	/* reason */
		break;

	default:
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
			("%s: invalid mgmt frame type %u\n", __func__, type));
		senderr(EINVAL, is_tx_unknownmgt);
		/* NOTREACHED */
	}

	ieee80211_mgmt_output(ic, ni, skb, type);
	if (timer)
		ic->ic_mgt_timer = timer;
	return 0;
bad:
	if (ni != ic->ic_bss)		/* remove ref we added */
		ieee80211_free_node(ic, ni);
	return ret;
#undef senderr
}

/*
 * Allocate a beacon frame and fillin the appropriate bits.
 */
struct sk_buff *
ieee80211_beacon_alloc(struct ieee80211com *ic, struct ieee80211_node *ni,
	struct ieee80211_beacon_offsets *bo)
{
	struct net_device *dev = ic->ic_dev;
	struct ieee80211_frame *wh;
	struct sk_buff *skb;
	int pktlen;
	u_int8_t *frm, *efrm;
	u_int16_t capinfo;
	struct ieee80211_rateset *rs;

	/*
	 * beacon frame format
	 *	[8] time stamp
	 *	[2] beacon interval
	 *	[2] cabability information
	 *	[tlv] ssid
	 *	[tlv] supported rates
	 *	[3] parameter set (DS)
	 *	[tlv] parameter set (IBSS/TIM)
	 *	[tlv] extended rate phy (ERP)
	 *	[tlv] extended supported rates
	 *	[tlv] WPA/RSN parameters
	 * XXX WME, etc.
	 * XXX Vendor-specific OIDs (e.g. Atheros)
	 */
	rs = &ni->ni_rates;
	/* XXX may be better to just allocate a max-sized buffer */
	pktlen =   8					/* time stamp */
		 + sizeof(u_int16_t)			/* beacon interval */
		 + sizeof(u_int16_t)			/* capabilities */
		 + 2 + ni->ni_esslen			/* ssid */
	         + 2 + IEEE80211_RATE_SIZE		/* supported rates */
	         + 2 + 1				/* DS parameters */
		 + 2 + 4				/* DTIM/IBSSPARMS */
		 + 2 + 1				/* ERP */
	         + 2 + (IEEE80211_RATE_MAXSIZE - IEEE80211_RATE_SIZE)
		 + 2*sizeof(struct ieee80211_ie_wpa)	/* WPA 1+2 */
		 ;
	skb = ieee80211_getmgtframe(&frm, pktlen);
	if (skb == NULL) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
			("%s: cannot get buf; size %u\n", __func__, pktlen));
		ic->ic_stats.is_tx_nobuf++;
		return NULL;
	}

	memset(frm, 0, 8);	/* XXX timestamp is set by hardware */
	frm += 8;
	*(u_int16_t *)frm = cpu_to_le16(ni->ni_intval);
	frm += 2;
	if (ic->ic_opmode == IEEE80211_M_IBSS)
		capinfo = IEEE80211_CAPINFO_IBSS;
	else
		capinfo = IEEE80211_CAPINFO_ESS;
	if (ic->ic_flags & IEEE80211_F_PRIVACY)
		capinfo |= IEEE80211_CAPINFO_PRIVACY;
	if ((ic->ic_flags & IEEE80211_F_SHPREAMBLE) &&
	    IEEE80211_IS_CHAN_2GHZ(ni->ni_chan))
		capinfo |= IEEE80211_CAPINFO_SHORT_PREAMBLE;
	if (ic->ic_flags & IEEE80211_F_SHSLOT)
		capinfo |= IEEE80211_CAPINFO_SHORT_SLOTTIME;
	bo->bo_caps = (u_int16_t *)frm;
	*(u_int16_t *)frm = cpu_to_le16(capinfo);
	frm += 2;
	*frm++ = IEEE80211_ELEMID_SSID;
	if ((ic->ic_flags & IEEE80211_F_HIDESSID) == 0) {
		*frm++ = ni->ni_esslen;
		memcpy(frm, ni->ni_essid, ni->ni_esslen);
		frm += ni->ni_esslen;
	} else
		*frm++ = 0;
	frm = ieee80211_add_rates(frm, rs);
	if (ic->ic_curmode != IEEE80211_MODE_FH) {
		*frm++ = IEEE80211_ELEMID_DSPARMS;
		*frm++ = 1;
		*frm++ = ieee80211_chan2ieee(ic, ni->ni_chan);
	}
	bo->bo_tim = frm;
	if (ic->ic_opmode == IEEE80211_M_IBSS) {
		*frm++ = IEEE80211_ELEMID_IBSSPARMS;
		*frm++ = 2;
		*frm++ = 0; *frm++ = 0;		/* TODO: ATIM window */
		bo->bo_tim_len = 4;
	} else {
		/* TODO: TIM */
		*frm++ = IEEE80211_ELEMID_TIM;
		*frm++ = 4;	/* length */
		*frm++ = 0;	/* DTIM count */ 
		*frm++ = 1;	/* DTIM period */
		*frm++ = 0;	/* bitmap control */
		*frm++ = 0;	/* Partial Virtual Bitmap (variable length) */
		bo->bo_tim_len = 6;
	}
	bo->bo_trailer = frm;
	if (ic->ic_flags & IEEE80211_F_WPA)
		frm = ieee80211_add_wpa(frm, ic);
	if (ic->ic_curmode == IEEE80211_MODE_11G)
		frm = ieee80211_add_erp(frm, ic);
	efrm = ieee80211_add_xrates(frm, rs);
	bo->bo_trailer_len = efrm - bo->bo_trailer;
	skb_trim(skb, efrm - skb->data);

	wh = (struct ieee80211_frame *)
		skb_push(skb, sizeof(struct ieee80211_frame));
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_MGT |
	    IEEE80211_FC0_SUBTYPE_BEACON;
	wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	*(u_int16_t *)wh->i_dur = 0;
	IEEE80211_ADDR_COPY(wh->i_addr1, dev->broadcast);
	IEEE80211_ADDR_COPY(wh->i_addr2, ic->ic_myaddr);
	IEEE80211_ADDR_COPY(wh->i_addr3, ni->ni_bssid);
	*(u_int16_t *)wh->i_seq = 0;

	return skb;
}
EXPORT_SYMBOL(ieee80211_beacon_alloc);

/*
 * Update the dynamic parts of a beacon frame based on the current state.
 */
int
ieee80211_beacon_update(struct ieee80211com *ic, struct ieee80211_node *ni,
	struct ieee80211_beacon_offsets *bo, struct sk_buff **skb0)
{
	u_int16_t capinfo;

	/* XXX lock out changes */
	/* XXX only update as needed */
	/* XXX faster to recalculate entirely or just changes? */
	if (ic->ic_opmode == IEEE80211_M_IBSS)
		capinfo = IEEE80211_CAPINFO_IBSS;
	else
		capinfo = IEEE80211_CAPINFO_ESS;
	if (ic->ic_flags & IEEE80211_F_PRIVACY)
		capinfo |= IEEE80211_CAPINFO_PRIVACY;
	if ((ic->ic_flags & IEEE80211_F_SHPREAMBLE) &&
	    IEEE80211_IS_CHAN_2GHZ(ni->ni_chan))
		capinfo |= IEEE80211_CAPINFO_SHORT_PREAMBLE;
	if (ic->ic_flags & IEEE80211_F_SHSLOT)
		capinfo |= IEEE80211_CAPINFO_SHORT_SLOTTIME;
	*bo->bo_caps = htole16(capinfo);

	if (ic->ic_flags & IEEE80211_F_TIMUPDATE) {
		/* 
		 * ATIM/DTIM needs updating.  If it fits in the
		 * current space allocated then just copy in the
		 * new bits.  Otherwise we need to move any extended
		 * rate set the follows and, possibly, allocate a
		 * new buffer if the this current one isn't large
		 * enough.  XXX It may be better to just allocate
		 * a max-sized buffer so we don't re-allocate.
		 */
		/* XXX fillin */
		ic->ic_flags &= ~IEEE80211_F_TIMUPDATE;
	}
	return 0;
}
EXPORT_SYMBOL(ieee80211_beacon_update);

/*
 * Save an outbound packet for a node in power-save sleep state.
 * The new packet is placed on the node's saved queue, and the TIM
 * is changed, if necessary.
 */
void
ieee80211_pwrsave(struct ieee80211com *ic, struct ieee80211_node *ni, 
		  struct sk_buff *skb)
{
	if (_IF_QLEN(&ni->ni_savedq) == 0)
		(*ic->ic_set_tim)(ic, ni->ni_associd, 1);
	if (_IF_QLEN(&ni->ni_savedq) >= IEEE80211_PS_MAX_QUEUE) {
		ni->ni_pwrsavedrops++;	/* XXX atomic_inc */
		dev_kfree_skb(skb);
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
			("station %s pwr save q overflow of size %d drops %d\n",
			ether_sprintf(ni->ni_macaddr), 
		        IEEE80211_PS_MAX_QUEUE, ni->ni_pwrsavedrops));
	} else {
		struct ieee80211_cb *cb = (struct ieee80211_cb *)skb->cb;
		/*
		 * Similar to ieee80211_mgmt_output, save the node in
		 * the packet for use by the driver start routine.
		 */
		/* XXX do we know we have a reference? */
		cb->ni = ni;
		IF_ENQUEUE(&ni->ni_savedq, skb);
	}
}
