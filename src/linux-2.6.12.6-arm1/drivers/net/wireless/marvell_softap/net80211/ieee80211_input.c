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
 * $Id: ieee80211_input.c,v 1.1.1.1 2009/08/19 08:02:21 jack Exp $
 */
#ifndef EXPORT_SYMTAB
#define	EXPORT_SYMTAB
#endif

__FBSDID("$FreeBSD: src/sys/net80211/ieee80211_input.c,v 1.13 2004/01/15 08:44:27 onoe Exp $");

/*
 * IEEE 802.11 input handling.
 */
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/random.h>
#include <linux/if_vlan.h>

#include "if_llc.h"
#include "if_ethersubr.h"
#include "if_media.h"

#include <net80211/ieee80211_var.h>

static struct sk_buff *ieee80211_defrag(struct ieee80211com *,
	struct ieee80211_node *, struct sk_buff *);
static struct sk_buff *ieee80211_decap(struct ieee80211com *, struct sk_buff *);
static void ieee80211_recv_pspoll(struct ieee80211com *,
	struct ieee80211_node *, struct sk_buff *);

#ifdef IEEE80211_DEBUG
/*
 * Decide if a received management frame should be
 * printed when debugging is enabled.  This filters some
 * of the less interesting frames that come frequently
 * (e.g. beacons).
 */
static __inline int
doprint(struct ieee80211com *ic, int subtype)
{
	switch (subtype) {
	case IEEE80211_FC0_SUBTYPE_BEACON:
		return (ic->ic_state == IEEE80211_S_SCAN);
	case IEEE80211_FC0_SUBTYPE_PROBE_REQ:
		return (ic->ic_opmode == IEEE80211_M_IBSS);
	}
	return 1;
}
#endif

/*
 * Process a received frame.  The node associated with the sender
 * should be supplied.  If nothing was found in the node table then
 * the caller is assumed to supply a reference to ic_bss instead.
 * The RSSI and a timestamp are also supplied.  The RSSI data is used
 * during AP scanning to select a AP to associate with; it can have
 * any units so long as values have consistent units and higher values
 * mean ``better signal''.  The receive timestamp is currently not used
 * by the 802.11 layer.
 */
void
ieee80211_input(struct ieee80211com *ic, struct sk_buff *skb,
	struct ieee80211_node *ni, int rssi, u_int32_t rstamp)
{
#define	SEQ_LEQ(a,b)	((int)((a)-(b)) <= 0)
#define	HAS_SEQ(type)	((type & 0x4) == 0)
	struct net_device *dev = ic->ic_dev;
	struct ieee80211_frame *wh;
	struct ieee80211_key *key;
	struct ether_header *eh;
	int len;
	u_int8_t dir, type, subtype;
	u_int8_t *bssid;
	u_int16_t rxseq;

	KASSERT(ni != NULL, ("null node"));
	KASSERT(skb->len >= sizeof(struct ieee80211_frame_min),
		("frame length too short: %u", skb->len));
	/*
	 * In monitor mode, send everything directly to bpf.
	 * Also do not process frames w/o i_addr2 any further.
	 * XXX may want to include the CRC
	 */
	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		goto out;

	if (skb->len < sizeof(struct ieee80211_frame_min)) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
			("%s: frame too short, len %u\n",
			__func__, skb->len));
		ic->ic_stats.is_rx_tooshort++;
		goto out;
	}
	/*
	 * Bit of a cheat here, we use a pointer for a 3-address
	 * frame format but don't reference fields past outside
	 * ieee80211_frame_min w/o first validating the data is
	 * present.
	 */
	wh = (struct ieee80211_frame *)skb->data;

	if ((wh->i_fc[0] & IEEE80211_FC0_VERSION_MASK) !=
	    IEEE80211_FC0_VERSION_0) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
			("receive packet with wrong version: %x\n",
			wh->i_fc[0]));
		ic->ic_stats.is_rx_badversion++;
		goto err;
	}

	dir = wh->i_fc[1] & IEEE80211_FC1_DIR_MASK;
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
	if (ic->ic_state != IEEE80211_S_SCAN) {
		switch (ic->ic_opmode) {
		case IEEE80211_M_STA:
			bssid = wh->i_addr2;
			if (!IEEE80211_ADDR_EQ(bssid, ni->ni_bssid)) {
				/* not interested in */
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_INPUT,
					("[%s] discard frame not to bss\n",
					ether_sprintf(bssid)));
				ic->ic_stats.is_rx_wrongbss++;
				goto out;
			}
			break;
		case IEEE80211_M_IBSS:
		case IEEE80211_M_AHDEMO:
		case IEEE80211_M_HOSTAP:
			if (dir != IEEE80211_FC1_DIR_NODS)
				bssid = wh->i_addr1;
			else if (type == IEEE80211_FC0_TYPE_CTL)
				bssid = wh->i_addr1;
			else {
				if (skb->len < sizeof(struct ieee80211_frame)) {
					IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
					    ("%s: frame too short, len %u\n",
					    __func__, skb->len));
					ic->ic_stats.is_rx_tooshort++;
					goto out;
				}
				bssid = wh->i_addr3;
			}
			if (type == IEEE80211_FC0_TYPE_DATA &&
			    !IEEE80211_ADDR_EQ(bssid, ic->ic_bss->ni_bssid) &&
			    !IEEE80211_ADDR_EQ(bssid, dev->broadcast)) {
				/* not interested in */
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_INPUT,
				    ("[%s] discard data frame not to bss\n",
				    ether_sprintf(bssid)));
				ic->ic_stats.is_rx_wrongbss++;
				goto out;
			}
			break;
		default:
			/* XXX catch bad values */
			goto out;
		}
		ni->ni_rssi = rssi;
		ni->ni_rstamp = rstamp;
		if (HAS_SEQ(type)) {
			rxseq = le16toh(*(u_int16_t *)wh->i_seq);
			if ((wh->i_fc[1] & IEEE80211_FC1_RETRY) &&
			    SEQ_LEQ(rxseq, ni->ni_rxseq)) {
				/* duplicate, discard */
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_INPUT,
				    ("[%s] discard duplicate frame, "
				    "seqno <%u,%u> fragno <%u,%u>\n",
				    ether_sprintf(bssid),
				    rxseq >> IEEE80211_SEQ_SEQ_SHIFT,
				    ni->ni_rxseq >> IEEE80211_SEQ_SEQ_SHIFT,
				    rxseq & IEEE80211_SEQ_FRAG_MASK,
				    ni->ni_rxseq & IEEE80211_SEQ_FRAG_MASK));
				/* XXX per-station stat */
				ic->ic_stats.is_rx_dup++;
				goto out;
			}
			ni->ni_rxseq = rxseq;
		}
	}

	/*
	 * Check for ps-poll state change for the station.
	 * XXX is there a response when pspoll is not supported?
	 */
	if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
	    ic->ic_set_tim != NULL &&
	    ((wh->i_fc[1] & IEEE80211_FC1_PWR_MGT) ^
	    (ni->ni_flags & IEEE80211_NODE_PWR_MGT))) {
		/* XXX statistics? */
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_POWER,
			("[%s] power save mode %s\n",
			ether_sprintf(wh->i_addr2),
			(wh->i_fc[1] & IEEE80211_FC1_PWR_MGT ? "on" : "off")));
		if ((wh->i_fc[1] & IEEE80211_FC1_PWR_MGT) == 0) {
			/* turn off power save mode, dequeue stored packets */
			ni->ni_flags &= ~IEEE80211_NODE_PWR_MGT;
			(*ic->ic_set_tim)(ic, ni->ni_associd, 0);
			while (!_IF_QLEN(&ni->ni_savedq) != 0) {
				struct sk_buff *skb0;
				IF_DEQUEUE(&ni->ni_savedq, skb0);
				/* XXX need different driver interface */
				(*dev->hard_start_xmit)(skb0, dev);/* XXX??? */
			}
		} else {
			/* turn on power save mode */
			ni->ni_flags |= IEEE80211_NODE_PWR_MGT;
		}
	}
	switch (type) {
	case IEEE80211_FC0_TYPE_DATA:
		if (skb->len < sizeof(struct ieee80211_frame)) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
				("%s: data frame too short, len %u\n",
				__func__, skb->len));
			ic->ic_stats.is_rx_tooshort++;
			goto out;		/* XXX */
		}
		switch (ic->ic_opmode) {
		case IEEE80211_M_STA:
			if (dir != IEEE80211_FC1_DIR_FROMDS) {
				ic->ic_stats.is_rx_wrongdir++;
				printk("%s: discard not from DS\n", __func__);
				goto out;
			}
			if ((dev->flags & IFF_MULTICAST) &&
			    IEEE80211_IS_MULTICAST(wh->i_addr1) &&
			    IEEE80211_ADDR_EQ(wh->i_addr3, ic->ic_myaddr)) {
				/*
				 * In IEEE802.11 network, multicast packet
				 * sent from me is broadcasted from AP.
				 * It should be silently discarded for
				 * SIMPLEX interface.
				 *
				 * NB: Linux has no IFF_ flag to indicate
				 *     if an interface is SIMPLEX or not;
				 *     so we always assume it to be true.
				 */
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_INPUT,
					("discard multicast echo\n"));
				printk("%s: discard MC echo\n", __func__);
				ic->ic_stats.is_rx_mcastecho++;
				goto out;
			}
			break;
		case IEEE80211_M_IBSS:
		case IEEE80211_M_AHDEMO:
			if (dir != IEEE80211_FC1_DIR_NODS) {
				ic->ic_stats.is_rx_wrongdir++;
				goto out;
			}
			break;
		case IEEE80211_M_HOSTAP:
			if (dir != IEEE80211_FC1_DIR_TODS) {
				ic->ic_stats.is_rx_wrongdir++;
				goto out;
			}
			/* check if source STA is associated */
			if (ni == ic->ic_bss) {
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_INPUT,
					("[%s] discard data from unknown src\n",
					ether_sprintf(wh->i_addr2)));

				/* NB: caller deals with reference to ic_bss */
				ni = ieee80211_dup_bss(ic, wh->i_addr2);
				if (ni != NULL) {
					IEEE80211_SEND_MGMT(ic, ni,
					    IEEE80211_FC0_SUBTYPE_DEAUTH,
					    IEEE80211_REASON_NOT_AUTHED);
					ieee80211_free_node(ic, ni);
				}
				ic->ic_stats.is_rx_notassoc++;
				goto err;
			}
			if (ni->ni_associd == 0) {
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_INPUT,
					("[%s] discard data from unassoc src\n",
					ether_sprintf(wh->i_addr2)));
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_DISASSOC,
				    IEEE80211_REASON_NOT_ASSOCED);
				ic->ic_stats.is_rx_notassoc++;
				goto err;
			}
			break;
		default:
			/* XXX here to keep compiler happy */
			goto out;
		}

		/*
		 * Handle privacy requirements.  Note that we
		 * must not be preempted from here until after
		 * we (potentially) call ieee80211_crypto_demic;
		 * otherwise we may violate assumptions in the
		 * crypto cipher modules used to do delayed update
		 * of replay sequence numbers.
		 */
		if (wh->i_fc[1] & IEEE80211_FC1_WEP) {
			if ((ic->ic_flags & IEEE80211_F_PRIVACY) == 0) {
				/*
				 * Discard encrypted frames when privacy is off.
				 */
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_INPUT,
					("[%s] discard WEP data frame 'cuz "
					"PRIVACY off\n",
					ether_sprintf(wh->i_addr2)));
				ic->ic_stats.is_rx_noprivacy++;
				goto out;
			}
			key = ieee80211_crypto_decap(ic, ni, skb);
			if (key == NULL) {
				/* NB: stats+msgs handled in crypto_decap */
				goto out;
			}
		} else {
			key = NULL;
		}

		/*
		 * Next up, any fragmentation.
		 */
		skb = ieee80211_defrag(ic, ni, skb);
		if (skb == NULL) {
			/* XXX statistic */
			/* Fragment dropped or frame not complete yet */
			goto out;
		}
		wh = NULL;		/* no longer valid, catch any uses */

		/*
		 * Next strip any MSDU crypto bits.
		 */
		if (key != NULL && !ieee80211_crypto_demic(ic, key, skb)) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_INPUT,
				("%s: discard frame on demic error\n",
				__func__));
			/* XXX statistic? */
			goto out;
		}

		/*
		 * Finally, strip the 802.11 header.
		 */
		skb = ieee80211_decap(ic, skb);
		if (skb == NULL) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_INPUT,
				("%s: decapsulation error\n", __func__));
			ic->ic_stats.is_rx_decap++;
			goto err;
		}
		eh = (struct ether_header *) skb->data;
		if ((ni->ni_flags & IEEE80211_NODE_AUTH) == 0) {
			/*
			 * Deny any non-PAE frames received prior to
			 * authorization.  For open/shared-key
			 * authentication the port is mark authorized
			 * after authentication completes.  For 802.1x
			 * the port is not marked authorized by the
			 * authenticator until the handshake has completed.
			 */
			if (eh->ether_type != __constant_htons(ETHERTYPE_PAE)) {
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_INPUT,
				    ("[%s] discard data (ether type 0x%x len %u)"
				    " on unauthorized port\n",
				    ether_sprintf(eh->ether_shost),
				    eh->ether_type, skb->len));
				ic->ic_stats.is_rx_unauth++;
				/* XXX node statistic */
				goto err;
			}
			ni->ni_inact = ic->ic_inact_auth;
		} else {
			/*
			 * When denying unencrypted frames, discard
			 * any non-PAE frames received without encryption.
			 */
			if ((ic->ic_flags & IEEE80211_F_DROPUNENC) &&
			    key == NULL &&
			    eh->ether_type != __constant_htons(ETHERTYPE_PAE)) {
				/*
				 * Drop unencrypted frames.
				 */
				ic->ic_stats.is_rx_unencrypted++;
				goto out;
			}
			ni->ni_inact = ic->ic_inact_run;
		}
		ic->ic_devstats->rx_packets++;
		ic->ic_devstats->rx_bytes += skb->len;

		/* perform as a bridge within the AP */
		if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
		    (ic->ic_flags & IEEE80211_F_NOBRIDGE) == 0) {
			struct sk_buff *skb1 = NULL;

			if (ETHER_IS_MULTICAST(eh->ether_dhost)) {
				skb1 = skb_copy(skb, GFP_ATOMIC);
			} else {
				/* XXX this dups work done in ieee80211_encap */
				/* check if destination is associated */
				struct ieee80211_node *ni1 =
				    ieee80211_find_node(ic, eh->ether_dhost);
				if (ni1 != NULL) {
					if (ni1->ni_associd != 0) {
						skb1 = skb;
						skb = NULL;
					}
					/* XXX statistic? */
					ieee80211_free_node(ic, ni1);
				}
			}
			if (skb1 != NULL) {
				len = skb1->len;
				skb1->dev = dev;
				skb1->mac.raw = skb1->data;
				skb1->nh.raw = skb1->data + 
					sizeof(struct ether_header);
				skb1->protocol = __constant_htons(ETH_P_802_2);
				dev_queue_xmit(skb1);
			}
		}
		if (skb != NULL) {
			skb->dev = dev;
			skb->protocol = eth_type_trans(skb, dev);
			if (ni->ni_vlan != 0 && ic->ic_vlgrp != NULL) {
				/* attach vlan tag */
				vlan_hwaccel_receive_skb(skb, ic->ic_vlgrp,
					ni->ni_vlan);
			} else {
				netif_rx(skb);
			}
			dev->last_rx = jiffies;
		}
		return;

	case IEEE80211_FC0_TYPE_MGT:
		if (dir != IEEE80211_FC1_DIR_NODS) {
			ic->ic_stats.is_rx_wrongdir++;
			goto err;
		}
		if (skb->len < sizeof(struct ieee80211_frame)) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
				("%s: mgt data frame too short, len %u\n",
				__func__, skb->len));
			ic->ic_stats.is_rx_tooshort++;
			goto out;
		}
#ifdef IEEE80211_DEBUG
		if ((ieee80211_msg_debug(ic) && doprint(ic, subtype)) ||
		    ieee80211_msg_dumppkts(ic)) {
			if_printf(ic->ic_dev, "[%s] received %s rssi %d\n",
			    ether_sprintf(wh->i_addr2),
			    ieee80211_mgt_subtype_name[subtype >>
				IEEE80211_FC0_SUBTYPE_SHIFT], rssi);
		}
#endif
		if (wh->i_fc[1] & IEEE80211_FC1_WEP) {
			if (subtype != IEEE80211_FC0_SUBTYPE_AUTH) {
				/*
				 * Only shared key auth frames with a challenge
				 * should be encrypted, discard all others.
				 */
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_INPUT,
					("[%s] discard %s with WEP\n",
					ether_sprintf(wh->i_addr2),
					ieee80211_mgt_subtype_name[subtype >>
					    IEEE80211_FC0_SUBTYPE_SHIFT]));
				ic->ic_stats.is_rx_mgtdiscard++; /* XXX */
				goto out;
			}
			if ((ic->ic_flags & IEEE80211_F_PRIVACY) == 0) {
				/*
				 * Discard encrypted frames when privacy is off.
				 */
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_INPUT,
					("[%s] discard WEP mgt frame 'cuz "
					"PRIVACY off\n",
					ether_sprintf(wh->i_addr2)));
				ic->ic_stats.is_rx_noprivacy++;
				goto out;
			}
			key = ieee80211_crypto_decap(ic, ni, skb);
			if (key == NULL) {
				/* NB: stats+msgs handled in crypto_decap */
				goto out;
			}
		}
		(*ic->ic_recv_mgmt)(ic, skb, ni, subtype, rssi, rstamp);
		dev_kfree_skb(skb);
		return;

	case IEEE80211_FC0_TYPE_CTL:
		ic->ic_stats.is_rx_ctl++;
		if (ic->ic_opmode != IEEE80211_M_HOSTAP)
			goto out;
		switch (subtype) {
		case IEEE80211_FC0_SUBTYPE_PS_POLL:
			/* XXX statistic */
			/* Dump out a single packet from the host */
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_POWER,
				("got power save probe from %s\n",
				ether_sprintf(wh->i_addr2)));
			ieee80211_recv_pspoll(ic, ni, skb);
			break;
		}
		goto out;

	default:
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
			("%s: bad frame type %x\n", __func__, type));
		/* should not come here */
		break;
	}
err:
	ic->ic_devstats->rx_errors++;
out:
	if (skb != NULL) {
		dev_kfree_skb(skb);
	}
#undef HAS_SEQ
#undef SEQ_LEQ
}
EXPORT_SYMBOL(ieee80211_input);

/*
 * This function reassemble fragments using the skb of the 1st fragment,
 * if large enough. If not, a new skb is allocated to hold incoming
 * fragments.
 *
 * Fragments are copied at the end of the previous fragment.  A different
 * strategy could have been used, where a non-linear skb is allocated and
 * fragments attached to that skb.
 */
static struct sk_buff *
ieee80211_defrag(struct ieee80211com *ic, struct ieee80211_node *ni,
	struct sk_buff *skb)
{
	struct ieee80211_frame *wh = (struct ieee80211_frame *) skb->data;
	u_int16_t rxseq, last_rxseq;
	u_int8_t fragno, last_fragno;
	u_int8_t more_frag = wh->i_fc[1] & IEEE80211_FC1_MORE_FRAG;

	if (IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		/* Do not keep fragments of multicast frames */
		return skb;
	}
		
	rxseq = le16_to_cpu(*(u_int16_t *)wh->i_seq) >> IEEE80211_SEQ_SEQ_SHIFT;
	fragno = le16_to_cpu(*(u_int16_t *)wh->i_seq) & IEEE80211_SEQ_FRAG_MASK;

	/* Quick way out, if there's nothing to defragment */
	if (!more_frag && fragno == 0 && ni->ni_rxfrag[0] == NULL)
		return skb;

	/*
	 * Use this lock to make sure ni->ni_rxfrag[0] is
	 * not freed by the timer process while we use it.
	 * XXX bogus
	 */
	IEEE80211_NODE_LOCK(ic);

	/*
	 * Update the time stamp.  As a side effect, it
	 * also makes sure that the timer will not change
	 * ni->ni_rxfrag[0] for at least 1 second, or in
	 * other words, for the remaining of this function.
	 */
	ni->ni_rxfragstamp = jiffies;

	IEEE80211_NODE_UNLOCK(ic);

	/*
	 * Validate that fragment is in order and
	 * related to the previous ones.
	 */
	if (ni->ni_rxfrag[0]) {
		struct ieee80211_frame *lwh;

		lwh = (struct ieee80211_frame *) ni->ni_rxfrag[0]->data;
		last_rxseq = le16_to_cpu(*(u_int16_t *)lwh->i_seq) >>
			IEEE80211_SEQ_SEQ_SHIFT;
		last_fragno = le16_to_cpu(*(u_int16_t *)lwh->i_seq) &
			IEEE80211_SEQ_FRAG_MASK;
		if (rxseq != last_rxseq
		    || fragno != last_fragno + 1
		    || (!IEEE80211_ADDR_EQ(wh->i_addr1, lwh->i_addr1))
		    || (!IEEE80211_ADDR_EQ(wh->i_addr2, lwh->i_addr2))
		    || (ni->ni_rxfrag[0]->end - ni->ni_rxfrag[0]->tail <
				skb->len)) {
			/*
			 * Unrelated fragment or no space for it,
			 * clear current fragments
			 */
			dev_kfree_skb(ni->ni_rxfrag[0]);
			ni->ni_rxfrag[0] = NULL;
		}
	}

	/* If this is the first fragment */
 	if (ni->ni_rxfrag[0] == NULL && fragno == 0) {
		ni->ni_rxfrag[0] = skb;
		/* If more frags are coming */
		if (more_frag) {
			if (skb_is_nonlinear(skb)) {
				/*
				 * We need a continous buffer to
				 * assemble fragments
				 */
				ni->ni_rxfrag[0] = skb_copy(skb, GFP_ATOMIC);
				dev_kfree_skb(skb);
			}
			/*
			 * Check that we have enough space to hold
			 * incoming fragments
			 */
			else if (skb->end - skb->head < ic->ic_dev->mtu +
				sizeof(sizeof(struct ieee80211_frame))) {
				ni->ni_rxfrag[0] = skb_copy_expand(skb, 0,
					(ic->ic_dev->mtu +
					 sizeof(sizeof(struct ieee80211_frame)))
				         - (skb->end - skb->head), GFP_ATOMIC);
				dev_kfree_skb(skb);
			}
		}
	} else {
		if (ni->ni_rxfrag[0]) {
			struct ieee80211_frame *lwh = (struct ieee80211_frame *)
				ni->ni_rxfrag[0]->data;

			/*
			 * We know we have enough space to copy,
			 * we've verified that before
			 */
			/* Copy current fragment at end of previous one */
			memcpy(ni->ni_rxfrag[0]->tail,
			       skb->data + sizeof(struct ieee80211_frame),
			       skb->len - sizeof(struct ieee80211_frame)
			);
			/* Update tail and length */
			skb_put(ni->ni_rxfrag[0],
				skb->len - sizeof(struct ieee80211_frame));
			/* Keep a copy of last sequence and fragno */
			*(u_int16_t *) lwh->i_seq = *(u_int16_t *) wh->i_seq;
		}
		/* we're done with the fragment */
		dev_kfree_skb(skb);
	}
		
	if (more_frag) {
		/* More to come */
		skb = NULL;
	} else {
		/* Last fragment received, we're done! */
		skb = ni->ni_rxfrag[0];
		ni->ni_rxfrag[0] = NULL;
	}
	return skb;
}

static struct sk_buff *
ieee80211_decap(struct ieee80211com *ic, struct sk_buff *skb)
{
	struct ether_header *eh;
	struct ieee80211_frame wh;
	struct llc *llc;
	u_short ether_type = 0;

	memcpy(&wh, skb->data, sizeof(struct ieee80211_frame));
	llc = (struct llc *) skb_pull(skb, sizeof(struct ieee80211_frame));
	if (skb->len >= sizeof(struct llc) &&
	    llc->llc_dsap == LLC_SNAP_LSAP && llc->llc_ssap == LLC_SNAP_LSAP &&
	    llc->llc_control == LLC_UI && llc->llc_snap.org_code[0] == 0 &&
	    llc->llc_snap.org_code[1] == 0 && llc->llc_snap.org_code[2] == 0) {
		ether_type = llc->llc_un.type_snap.ether_type;
		skb_pull(skb, sizeof(struct llc));
		llc = NULL;
	}
	eh = (struct ether_header *) skb_push(skb, sizeof(struct ether_header));
	switch (wh.i_fc[1] & IEEE80211_FC1_DIR_MASK) {
	case IEEE80211_FC1_DIR_NODS:
		IEEE80211_ADDR_COPY(eh->ether_dhost, wh.i_addr1);
		IEEE80211_ADDR_COPY(eh->ether_shost, wh.i_addr2);
		break;
	case IEEE80211_FC1_DIR_TODS:
		IEEE80211_ADDR_COPY(eh->ether_dhost, wh.i_addr3);
		IEEE80211_ADDR_COPY(eh->ether_shost, wh.i_addr2);
		break;
	case IEEE80211_FC1_DIR_FROMDS:
		IEEE80211_ADDR_COPY(eh->ether_dhost, wh.i_addr1);
		IEEE80211_ADDR_COPY(eh->ether_shost, wh.i_addr3);
		break;
	case IEEE80211_FC1_DIR_DSTODS:
		/* not yet supported */
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
			("%s: discard DS to DS frame\n", __func__));
		dev_kfree_skb(skb);
		return NULL;
	}
	if (!ALIGNED_POINTER(skb->data + sizeof(*eh), u_int32_t)) {
		struct sk_buff *n;

		/* XXX does this always work? */
		n = skb_copy(skb, GFP_ATOMIC);
		dev_kfree_skb(skb);
		if (n == NULL)
			return NULL;
		skb = n;
		eh = (struct ether_header *) skb->data;
	}
	if (llc != NULL)
		eh->ether_type = htons(skb->len - sizeof(*eh));
	else
		eh->ether_type = ether_type;
	return skb;
}

/*
 * Install received rate set information in the node's state block.
 */
static int
ieee80211_setup_rates(struct ieee80211com *ic, struct ieee80211_node *ni,
	u_int8_t *rates, u_int8_t *xrates, int flags)
{
	struct ieee80211_rateset *rs = &ni->ni_rates;

	memset(rs, 0, sizeof(*rs));
	rs->rs_nrates = rates[1];
	memcpy(rs->rs_rates, rates + 2, rs->rs_nrates);
	if (xrates != NULL) {
		u_int8_t nxrates;
		/*
		 * Tack on 11g extended supported rate element.
		 */
		nxrates = xrates[1];
		if (rs->rs_nrates + nxrates > IEEE80211_RATE_MAXSIZE) {
			nxrates = IEEE80211_RATE_MAXSIZE - rs->rs_nrates;
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_XRATE,
				("%s: extended rate set too large;"
				" only using %u of %u rates\n",
				__func__, nxrates, xrates[1]));
			ic->ic_stats.is_rx_rstoobig++;
		}
		memcpy(rs->rs_rates + rs->rs_nrates, xrates+2, nxrates);
		rs->rs_nrates += nxrates;
	}
	return ieee80211_fix_rate(ic, ni, flags);
}

static void
ieee80211_auth_open(struct ieee80211com *ic, struct ieee80211_frame *wh,
    struct ieee80211_node *ni, int rssi, u_int32_t rstamp, u_int16_t seq,
    u_int16_t status)
{
	int allocbs;

	switch (ic->ic_opmode) {
	case IEEE80211_M_IBSS:
		if (ic->ic_state != IEEE80211_S_RUN ||
		    seq != IEEE80211_AUTH_OPEN_REQUEST) {
			ic->ic_stats.is_rx_bad_auth++;
			return;
		}
		ieee80211_new_state(ic, IEEE80211_S_AUTH,
		    wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK);
		break;

	case IEEE80211_M_AHDEMO:
		/* should not come here */
		break;

	case IEEE80211_M_HOSTAP:
		if (ic->ic_state != IEEE80211_S_RUN ||
		    seq != IEEE80211_AUTH_OPEN_REQUEST) {
			ic->ic_stats.is_rx_bad_auth++;
			return;
		}
		/* always accept open authentication requests */
		if (ni == ic->ic_bss) {
			ni = ieee80211_dup_bss(ic, wh->i_addr2);
			if (ni == NULL)
				return;
			allocbs = 1;
		} else
			allocbs = 0;
		ni->ni_rssi = rssi;
		ni->ni_rstamp = rstamp;
		IEEE80211_SEND_MGMT(ic, ni,
			IEEE80211_FC0_SUBTYPE_AUTH, seq + 1);
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_DEBUG | IEEE80211_MSG_AUTH,
			("station %s %s authenticated (open)\n",
			ether_sprintf(ni->ni_macaddr),
			(allocbs ? "newly" : "already")));
		break;

	case IEEE80211_M_STA:
		if (ic->ic_state != IEEE80211_S_AUTH ||
		    seq != IEEE80211_AUTH_OPEN_RESPONSE) {
			ic->ic_stats.is_rx_bad_auth++;
			return;
		}
		if (status != 0) {
			IEEE80211_DPRINTF(ic,
			    IEEE80211_MSG_DEBUG | IEEE80211_MSG_AUTH,
			    ("open authentication failed (reason %d) for %s\n",
			    status,
			    ether_sprintf(wh->i_addr3)));
			/* XXX can this happen? */
			if (ni != ic->ic_bss)
				ni->ni_fails++;
			ic->ic_stats.is_rx_auth_fail++;
			return;
		}
		ieee80211_new_state(ic, IEEE80211_S_ASSOC,
		    wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK);
		break;
	case IEEE80211_M_MONITOR:
		break;
	}
}

static int
alloc_challenge(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	if (ni->ni_challenge == NULL)
		MALLOC(ni->ni_challenge, u_int32_t*, IEEE80211_CHALLENGE_LEN,
		    M_DEVBUF, M_NOWAIT);
	if (ni->ni_challenge == NULL) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_DEBUG | IEEE80211_MSG_AUTH,
			("%s: challenge alloc failed\n", __func__));
		/* XXX statistic */
	}
	return (ni->ni_challenge != NULL);
}

/* XXX TODO: add statistics */
static void
ieee80211_auth_shared(struct ieee80211com *ic, struct ieee80211_frame *wh,
    u_int8_t *frm, u_int8_t *efrm, struct ieee80211_node *ni, int rssi,
    u_int32_t rstamp, u_int16_t seq, u_int16_t status)
{
	u_int8_t *challenge;
	int allocbs, estatus;

	/*
	 * NB: this can happen as we allow pre-shared key
	 * authentication to be enabled w/o wep being turned
	 * on so that configuration of these can be done
	 * in any order.  It may be better to enforce the
	 * ordering in which case this check would just be
	 * for sanity/consistency.
	 */
	if ((ic->ic_flags & IEEE80211_F_PRIVACY) == 0) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_AUTH,
			("%s: WEP is off\n", __func__));
		estatus = IEEE80211_STATUS_ALG;
		goto bad;
	}
	/*
	 * Pre-shared key authentication is evil; accept
	 * it only if explicitly configured (it is supported
	 * mainly for compatibility with clients like OS X).
	 */
	if (ni->ni_authmode != IEEE80211_AUTH_AUTO &&
	    ni->ni_authmode != IEEE80211_AUTH_SHARED) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_AUTH,
			("%s: operating in %u mode, reject\n",
			 __func__, ni->ni_authmode));
		ic->ic_stats.is_rx_bad_auth++;	/* XXX maybe a unique error? */
		estatus = IEEE80211_STATUS_ALG;
		goto bad;
	}

	challenge = NULL;
	if (frm + 1 < efrm) {
		if ((frm[1] + 2) > (efrm - frm)) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_AUTH,
				("%s: elt %d %d bytes too long\n", __func__,
				frm[0], (frm[1] + 2) - (efrm - frm)));
			ic->ic_stats.is_rx_bad_auth++;
			estatus = IEEE80211_STATUS_CHALLENGE;
			goto bad;
		}
		if (*frm == IEEE80211_ELEMID_CHALLENGE)
			challenge = frm;
		frm += frm[1] + 2;
	}
	switch (seq) {
	case IEEE80211_AUTH_SHARED_CHALLENGE:
	case IEEE80211_AUTH_SHARED_RESPONSE:
		if (challenge == NULL) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_AUTH,
				("%s: no challenge sent\n", __func__));
			ic->ic_stats.is_rx_bad_auth++;
			estatus = IEEE80211_STATUS_CHALLENGE;
			goto bad;
		}
		if (challenge[1] != IEEE80211_CHALLENGE_LEN) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_AUTH,
				("%s: bad challenge len %d\n",
				__func__, challenge[1]));
			ic->ic_stats.is_rx_bad_auth++;
			estatus = IEEE80211_STATUS_CHALLENGE;
			goto bad;
		}
	default:
		break;
	}
	switch (ic->ic_opmode) {
	case IEEE80211_M_MONITOR:
	case IEEE80211_M_AHDEMO:
	case IEEE80211_M_IBSS:
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_AUTH,
			("%s: unexpected operating mode\n", __func__));
		return;
	case IEEE80211_M_HOSTAP:
		if (ic->ic_state != IEEE80211_S_RUN) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_AUTH,
				("%s: not running\n", __func__));
			estatus = IEEE80211_STATUS_ALG;	/* XXX */
			goto bad;
		}
		switch (seq) {
		case IEEE80211_AUTH_SHARED_REQUEST:
			if (ni == ic->ic_bss) {
				ni = ieee80211_dup_bss(ic, wh->i_addr2);
				if (ni == NULL) {
					/* NB: no way to return an error */
					return;
				}
				allocbs = 1;
			} else
				allocbs = 0;
			ni->ni_rssi = rssi;
			ni->ni_rstamp = rstamp;
			if (!alloc_challenge(ic, ni)) {
				/* NB: don't return error so they rexmit */
				return;
			}
			get_random_bytes(ni->ni_challenge,
				IEEE80211_CHALLENGE_LEN);
			IEEE80211_DPRINTF(ic,
				IEEE80211_MSG_DEBUG | IEEE80211_MSG_AUTH,
				("shared key %sauth request from station %s\n",
				(allocbs ? "" : "re"),
				ether_sprintf(ni->ni_macaddr)));
			break;
		case IEEE80211_AUTH_SHARED_RESPONSE:
			if (ni == ic->ic_bss) {
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_AUTH,
					("%s: unknown STA\n", __func__));
				/* NB: don't send a response */
				return;
			}
			if (ni->ni_challenge == NULL) {
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_AUTH,
				    ("%s: no challenge recorded\n", __func__));
				ic->ic_stats.is_rx_bad_auth++;
				estatus = IEEE80211_STATUS_CHALLENGE;
				goto bad;
			}
			if (memcmp(ni->ni_challenge, &challenge[2],
			           challenge[1]) != 0) {
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_AUTH,
					("%s: challenge mismatch\n", __func__));
				ic->ic_stats.is_rx_auth_fail++;
				estatus = IEEE80211_STATUS_CHALLENGE;
				goto bad;
			}
			IEEE80211_DPRINTF(ic,
				IEEE80211_MSG_DEBUG | IEEE80211_MSG_AUTH,
				("station %s authenticated (shared key)\n",
				ether_sprintf(ni->ni_macaddr)));
			break;
		default:
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_AUTH,
				("%s: bad shared key auth seq %d from %s\n",
				__func__, seq, ether_sprintf(wh->i_addr2)));
			ic->ic_stats.is_rx_bad_auth++;
			estatus = IEEE80211_STATUS_SEQUENCE;
			goto bad;
		}
		IEEE80211_SEND_MGMT(ic, ni,
			IEEE80211_FC0_SUBTYPE_AUTH, seq + 1);
		break;

	case IEEE80211_M_STA:
		if (ic->ic_state != IEEE80211_S_AUTH)
			return;
		switch (seq) {
		case IEEE80211_AUTH_SHARED_PASS:
			if (ni->ni_challenge != NULL) {
				FREE(ni->ni_challenge, M_DEVBUF);
				ni->ni_challenge = NULL;
			}
			if (status != 0) {
				IEEE80211_DPRINTF(ic,
				    IEEE80211_MSG_DEBUG | IEEE80211_MSG_AUTH,
				    ("%s: auth failed (reason %d) for %s\n",
				    __func__, status,
				    ether_sprintf(wh->i_addr3)));
				/* XXX can this happen? */
				if (ni != ic->ic_bss)
					ni->ni_fails++;
				ic->ic_stats.is_rx_auth_fail++;
				return;
			}
			ieee80211_new_state(ic, IEEE80211_S_ASSOC,
			    wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK);
			break;
		case IEEE80211_AUTH_SHARED_CHALLENGE:
			if (!alloc_challenge(ic, ni))
				return;
			/* XXX could optimize by passing recvd challenge */
			memcpy(ni->ni_challenge, &challenge[2], challenge[1]);
			IEEE80211_SEND_MGMT(ic, ni,
				IEEE80211_FC0_SUBTYPE_AUTH, seq + 1);
			break;
		default:
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_AUTH,
			    ("%s: bad seq %d from %s\n",
			    __func__, seq, ether_sprintf(wh->i_addr2)));
			ic->ic_stats.is_rx_bad_auth++;
			return;
		}
		break;
	}
	return;
bad:
	/*
	 * Send an error response; but only when operating as an AP.
	 */
	if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
		/* XXX hack to workaround calling convention */
		IEEE80211_SEND_MGMT(ic, ni,
			IEEE80211_FC0_SUBTYPE_AUTH,
			(seq + 1) | (estatus<<16));
	}
}

/* Verify the existence and length of __elem or get out. */
#define IEEE80211_VERIFY_ELEMENT(__elem, __maxlen) do {			\
	if ((__elem) == NULL) {						\
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID,		\
			("%s: no " #__elem "in %s frame\n",		\
			__func__, ieee80211_mgt_subtype_name[subtype >>	\
				IEEE80211_FC0_SUBTYPE_SHIFT]));		\
		ic->ic_stats.is_rx_elem_missing++;			\
		return;							\
	}								\
	if ((__elem)[1] > (__maxlen)) {					\
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID,		\
			("%s: bad " #__elem " len %d in %s frame from %s\n",\
			__func__, (__elem)[1],				\
			ieee80211_mgt_subtype_name[subtype >>		\
				IEEE80211_FC0_SUBTYPE_SHIFT],		\
			ether_sprintf(wh->i_addr2)));			\
		ic->ic_stats.is_rx_elem_toobig++;			\
		return;							\
	}								\
} while (0)

#define	IEEE80211_VERIFY_LENGTH(_len, _minlen) do {			\
	if ((_len) < (_minlen)) {					\
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID,		\
			("%s: %s frame too short from %s\n",		\
			__func__,					\
			ieee80211_mgt_subtype_name[subtype >>		\
				IEEE80211_FC0_SUBTYPE_SHIFT],		\
			ether_sprintf(wh->i_addr2)));			\
		ic->ic_stats.is_rx_elem_toosmall++;			\
		return;							\
	}								\
} while (0)

#ifdef IEEE80211_DEBUG
static void
ieee80211_ssid_mismatch(struct ieee80211com *ic, const char *tag,
	u_int8_t mac[IEEE80211_ADDR_LEN], u_int8_t *ssid)
{
	printf("[%s] %s req ssid mismatch: ", ether_sprintf(mac), tag);
	ieee80211_print_essid(ssid + 2, ssid[1]);
	printf("\n");
}

#define	IEEE80211_VERIFY_SSID(_ni, _ssid, _packet_type) do {		\
	if ((_ssid)[1] != 0 &&						\
	    ((_ssid)[1] != (_ni)->ni_esslen ||				\
	    memcmp((_ssid) + 2, (_ni)->ni_essid, (_ssid)[1]) != 0)) {	\
		if (ieee80211_msg_input(ic))				\
			ieee80211_ssid_mismatch(ic, _packet_type,	\
				wh->i_addr2, _ssid);			\
		ic->ic_stats.is_rx_ssidmismatch++;			\
		return;							\
	}								\
} while (0)
#else /* !IEEE80211_DEBUG */
#define	IEEE80211_VERIFY_SSID(_ni, _ssid, _packet_type) do {		\
	if ((_ssid)[1] != 0 &&						\
	    ((_ssid)[1] != (_ni)->ni_esslen ||				\
	    memcmp((_ssid) + 2, (_ni)->ni_essid, (_ssid)[1]) != 0)) {	\
		ic->ic_stats.is_rx_ssidmismatch++;			\
		return;							\
	}								\
} while (0)
#endif /* !IEEE80211_DEBUG */

/* unalligned little endian access */     
#define LE_READ_2(p)							\
	((u_int16_t)							\
	 ((((u_int8_t *)(p))[0]      ) | (((u_int8_t *)(p))[1] <<  8)))
#define LE_READ_4(p)							\
	((u_int32_t)							\
	 ((((u_int8_t *)(p))[0]      ) | (((u_int8_t *)(p))[1] <<  8) |	\
	  (((u_int8_t *)(p))[2] << 16) | (((u_int8_t *)(p))[3] << 24)))

static int __inline
iswpaoui(const u_int8_t *frm)
{
	return frm[1] > 3 && LE_READ_4(frm+2) == ((WPA_OUI_TYPE<<24)|WPA_OUI);
}

static int __inline
isatherosoui(const u_int8_t *frm)
{
	return frm[1] > 3 && LE_READ_4(frm+2) == ((ATH_OUI_TYPE<<24)|ATH_OUI);
}

/*
 * Convert a WPA cipher selector OUI to an internal
 * cipher algorithm.  Where appropriate we also
 * record any key length.
 */
static int
wpa_cipher(u_int8_t *sel, u_int8_t *keylen)
{
#define	WPA_SEL(x)	(((x)<<24)|WPA_OUI)
	u_int32_t w = LE_READ_4(sel);

	switch (w) {
	case WPA_SEL(WPA_CSE_NULL):
		return IEEE80211_CIPHER_NONE;
	case WPA_SEL(WPA_CSE_WEP40):
		if (keylen)
			*keylen = 40 / NBBY;
		return IEEE80211_CIPHER_WEP;
	case WPA_SEL(WPA_CSE_WEP104):
		if (keylen)
			*keylen = 104 / NBBY;
		return IEEE80211_CIPHER_WEP;
	case WPA_SEL(WPA_CSE_TKIP):
		return IEEE80211_CIPHER_TKIP;
	case WPA_SEL(WPA_CSE_CCMP):
		return IEEE80211_CIPHER_AES_CCM;
	}
	return 32;		/* NB: so 1<< is discarded */
#undef WPA_SEL
}

/*
 * Convert a WPA key management/authentication algorithm
 * to an internal code.
 */
static int
wpa_keymgmt(u_int8_t *sel)
{
#define	WPA_SEL(x)	(((x)<<24)|WPA_OUI)
	u_int32_t w = LE_READ_4(sel);

	switch (w) {
	case WPA_SEL(WPA_ASE_8021X_UNSPEC):
		return WPA_ASE_8021X_UNSPEC;
	case WPA_SEL(WPA_ASE_8021X_PSK):
		return WPA_ASE_8021X_PSK;
	case WPA_SEL(WPA_ASE_NONE):
		return WPA_ASE_NONE;
	}
	return 0;		/* NB: so is discarded */
#undef WPA_SEL
}

/*
 * Parse a WPA information element to collect parameters
 * and validate the parameters against what has been
 * configured for the system.
 */
static int
ieee80211_parse_wpa(struct ieee80211com *ic, u_int8_t *frm, struct ieee80211_rsnparms *rsn)
{
	u_int8_t len = frm[1];
	u_int32_t w;
	int n;

	/*
	 * Check the length once for fixed parts: OUI, type,
	 * version, mcast cipher, and 2 selector counts.
	 * Other, variable-length data, must be checked separately.
	 */
	KASSERT(ic->ic_flags & IEEE80211_F_WPA1,
		("not WPA, flags 0x%x", ic->ic_flags));
	if (len < 14) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
			("%s: length %u too short\n", __func__, len));
		return IEEE80211_REASON_IE_INVALID;
	}
	frm += 6, len -= 4;		/* NB: len is payload only */
	/* NB: iswapoui already validated the OUI and type */
	w = LE_READ_2(frm);
	if (w != WPA_VERSION) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
			("%s: bad version %u\n", __func__, w));
		return IEEE80211_REASON_IE_INVALID;
	}
	frm += 2, len -= 2;

	/* multicast/group cipher */
	w = wpa_cipher(frm, &rsn->rsn_mcastkeylen);
	if (w != rsn->rsn_mcastcipher) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
			("%s: mcast cipher mismatch; got %u, expected %u\n",
				__func__, w, rsn->rsn_mcastcipher));
		return IEEE80211_REASON_IE_INVALID;
	}
	frm += 4, len -= 4;

	/* unicast ciphers */
	n = LE_READ_2(frm);
	frm += 2, len -= 2;
	if (len < n*4+2) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
			("%s: not enough data for ucast ciphers; len %u, n %u\n",
				__func__, len, n));
		return IEEE80211_REASON_IE_INVALID;
	}
	w = 0;
	for (; n > 0; n--) {
		w |= 1<<wpa_cipher(frm, &rsn->rsn_ucastkeylen);
		frm += 4, len -= 4;
	}
	w &= rsn->rsn_ucastcipherset;
	if (w == 0) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
			("%s: ucast cipher set empty\n", __func__));
		return IEEE80211_REASON_IE_INVALID;
	}
	if (w & (1<<IEEE80211_CIPHER_TKIP))
		rsn->rsn_ucastcipher = IEEE80211_CIPHER_TKIP;
	else
		rsn->rsn_ucastcipher = IEEE80211_CIPHER_AES_CCM;

	/* key management algorithms */
	n = LE_READ_2(frm);
	frm += 2, len -= 2;
	if (len < n*4) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
			("%s: not enough data for key mgmt algorithms; len %u, n %u\n",
				__func__, len, n));
		return IEEE80211_REASON_IE_INVALID;
	}
	w = 0;
	for (; n > 0; n--) {
		w |= wpa_keymgmt(frm);
		frm += 4, len -= 4;
	}
	w &= rsn->rsn_keymgmtset;
	if (w == 0) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
			("%s: no acceptable key mgmt algorithms\n", __func__));
		return IEEE80211_REASON_IE_INVALID;
	}
	if (w & WPA_ASE_8021X_UNSPEC)
		rsn->rsn_keymgmt = WPA_ASE_8021X_UNSPEC;
	else
		rsn->rsn_keymgmt = WPA_ASE_8021X_PSK;

	if (len > 2)		/* optional capabilities */
		rsn->rsn_caps = LE_READ_2(frm);

	return 0;
}

/*
 * Convert an RSN cipher selector OUI to an internal
 * cipher algorithm.  Where appropriate we also
 * record any key length.
 */
static int
rsn_cipher(u_int8_t *sel, u_int8_t *keylen)
{
#define	RSN_SEL(x)	(((x)<<24)|RSN_OUI)
	u_int32_t w = LE_READ_4(sel);

	switch (w) {
	case RSN_SEL(RSN_CSE_NULL):
		return IEEE80211_CIPHER_NONE;
	case RSN_SEL(RSN_CSE_WEP40):
		if (keylen)
			*keylen = 40 / NBBY;
		return IEEE80211_CIPHER_WEP;
	case RSN_SEL(RSN_CSE_WEP104):
		if (keylen)
			*keylen = 104 / NBBY;
		return IEEE80211_CIPHER_WEP;
	case RSN_SEL(RSN_CSE_TKIP):
		return IEEE80211_CIPHER_TKIP;
	case RSN_SEL(RSN_CSE_CCMP):
		return IEEE80211_CIPHER_AES_CCM;
	case RSN_SEL(RSN_CSE_WRAP):
		return IEEE80211_CIPHER_AES_OCB;
	}
	return 32;		/* NB: so 1<< is discarded */
#undef WPA_SEL
}

/*
 * Convert an RSN key management/authentication algorithm
 * to an internal code.
 */
static int
rsn_keymgmt(u_int8_t *sel)
{
#define	RSN_SEL(x)	(((x)<<24)|RSN_OUI)
	u_int32_t w = LE_READ_4(sel);

	switch (w) {
	case RSN_SEL(RSN_ASE_8021X_UNSPEC):
		return RSN_ASE_8021X_UNSPEC;
	case RSN_SEL(RSN_ASE_8021X_PSK):
		return RSN_ASE_8021X_PSK;
	case RSN_SEL(RSN_ASE_NONE):
		return RSN_ASE_NONE;
	}
	return 0;		/* NB: so is discarded */
#undef RSN_SEL
}

/*
 * Parse a WPA/RSN information element to collect parameters
 * and validate the parameters against what has been
 * configured for the system.
 */
static int
ieee80211_parse_rsn(struct ieee80211com *ic, u_int8_t *frm, struct ieee80211_rsnparms *rsn)
{
	u_int8_t len = frm[1];
	u_int32_t w;
	int n;

	/*
	 * Check the length once for fixed parts: 
	 * version, mcast cipher, and 2 selector counts.
	 * Other, variable-length data, must be checked separately.
	 */
	KASSERT(ic->ic_flags & IEEE80211_F_WPA2,
		("not RSN, flags 0x%x", ic->ic_flags));
	if (len < 10) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
			("%s: length %u too short\n", __func__, len));
		return IEEE80211_REASON_IE_INVALID;
	}
	frm += 2, len -= 2;

	w = LE_READ_2(frm);
	if (w != RSN_VERSION) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
			("%s: bad version %u\n", __func__, w));
		return IEEE80211_REASON_IE_INVALID;
	}
	frm += 2, len -= 2;

	/* multicast/group cipher */
	w = rsn_cipher(frm, &rsn->rsn_mcastkeylen);
	if (w != rsn->rsn_mcastcipher) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
			("%s: mcast cipher mismatch; got %u, expected %u\n",
				__func__, w, rsn->rsn_mcastcipher));
		return IEEE80211_REASON_IE_INVALID;
	}
	frm += 4, len -= 4;

	/* unicast ciphers */
	n = LE_READ_2(frm);
	frm += 2, len -= 2;
	if (len < n*4+2) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
			("%s: not enough data for ucast ciphers; len %u, n %u\n",
				__func__, len, n));
		return IEEE80211_REASON_IE_INVALID;
	}
	w = 0;
	for (; n > 0; n--) {
		w |= 1<<rsn_cipher(frm, &rsn->rsn_ucastkeylen);
		frm += 4, len -= 4;
	}
	w &= rsn->rsn_ucastcipherset;
	if (w == 0) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
			("%s: ucast cipher set empty\n", __func__));
		return IEEE80211_REASON_IE_INVALID;
	}
	if (w & (1<<IEEE80211_CIPHER_TKIP))
		rsn->rsn_ucastcipher = IEEE80211_CIPHER_TKIP;
	else
		rsn->rsn_ucastcipher = IEEE80211_CIPHER_AES_CCM;

	/* key management algorithms */
	n = LE_READ_2(frm);
	frm += 2, len -= 2;
	if (len < n*4) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
			("%s: not enough data for key mgmt algorithms; len %u, n %u\n",
				__func__, len, n));
		return IEEE80211_REASON_IE_INVALID;
	}
	w = 0;
	for (; n > 0; n--) {
		w |= rsn_keymgmt(frm);
		frm += 4, len -= 4;
	}
	w &= rsn->rsn_keymgmtset;
	if (w == 0) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
			("%s: no acceptable key mgmt algorithms\n", __func__));
		return IEEE80211_REASON_IE_INVALID;
	}
	if (w & RSN_ASE_8021X_UNSPEC)
		rsn->rsn_keymgmt = RSN_ASE_8021X_UNSPEC;
	else
		rsn->rsn_keymgmt = RSN_ASE_8021X_PSK;

	/* optional RSN capabilities */
	if (len > 2)
		rsn->rsn_caps = LE_READ_2(frm);
	/* XXXPMKID */

	return 0;
}

static void
ieee80211_saveie(u_int8_t **iep, const u_int8_t *ie)
{
	u_int ielen = ie[1]+2;
	/*
	 * Record information element for later use.
	 */
	if (*iep == NULL || (*iep)[1] != ie[1]) {
		if (*iep != NULL)
			FREE(*iep, M_DEVBUF);
		MALLOC(*iep, void*, ielen, M_DEVBUF, M_NOWAIT);
	}
	if (*iep != NULL)
		memcpy(*iep, ie, ielen);
}

#ifdef IEEE80211_DEBUG
static void
dump_probe_beacon(u_int8_t subtype, int isnew,
	const u_int8_t mac[IEEE80211_ADDR_LEN],
	u_int8_t chan, u_int8_t bchan, u_int16_t capinfo, u_int16_t bintval,
	u_int8_t erp, u_int8_t *ssid, u_int8_t *country)
{
	printf("[%s] %s%s on chan %u (bss chan %u) ",
	    ether_sprintf(mac), isnew ? "new " : "",
	    (subtype == IEEE80211_FC0_SUBTYPE_PROBE_RESP) ? "probe response" : "beacon",
	    chan, bchan);
	ieee80211_print_essid(ssid + 2, ssid[1]);
	printf("\n");

	if (isnew) {
		printf("[%s] caps 0x%x bintval %u erp 0x%x", 
			ether_sprintf(mac), capinfo, bintval, erp);
		if (country) {
#ifdef __FreeBSD__
			printf(" country info %*D", country[1], country+2, " ");
#else
			int i;
			printf(" country info");
			for (i = 0; i < country[1]; i++)
				printf(" %02x", country[i+2]);
#endif
		}
		printf("\n");
	}
}
#endif /* IEEE80211_DEBUG */

void
ieee80211_recv_mgmt(struct ieee80211com *ic, struct sk_buff *skb,
	struct ieee80211_node *ni,
	int subtype, int rssi, u_int32_t rstamp)
{
#define	ISPROBE(_st)	((_st) == IEEE80211_FC0_SUBTYPE_PROBE_RESP)
#define	ISREASSOC(_st)	((_st) == IEEE80211_FC0_SUBTYPE_REASSOC_RESP)
	struct ieee80211_frame *wh;
	u_int8_t *frm, *efrm;
	u_int8_t *ssid, *rates, *xrates, *wpa;
	int reassoc, resp, allocbs;

	wh = (struct ieee80211_frame *) skb->data;
	frm = (u_int8_t *)&wh[1];
	efrm = skb->data + skb->len;
	switch (subtype) {
	case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
	case IEEE80211_FC0_SUBTYPE_BEACON: {
		u_int8_t *tstamp, *country, *wpa;
		u_int8_t chan, bchan, fhindex, erp;
		u_int16_t capinfo, bintval, timoff;
		u_int16_t fhdwell;

		if (subtype == IEEE80211_FC0_SUBTYPE_BEACON) {
			/*
			 * Count beacon frames specially, some drivers
			 * use this info to do things like update LED's.
			 */
			ic->ic_stats.is_rx_beacon++;
		}
		/*
		 * We process beacon/probe response frames for:
		 *    o station mode when associated: to collect state
		 *      updates such as 802.11g slot time
		 *    o adhoc mode: to discover neighbors
		 *    o when scanning
		 * Frames otherwise received are discarded.
		 */ 
		if (!((ic->ic_opmode == IEEE80211_M_STA && ni->ni_associd != 0)
		    || ic->ic_opmode == IEEE80211_M_IBSS
		    || ic->ic_state == IEEE80211_S_SCAN)) {
			/* XXX: may be useful for background scan */
			ic->ic_stats.is_rx_mgtdiscard++;
			return;
		}
		/*
		 * beacon/probe response frame format
		 *	[8] time stamp
		 *	[2] beacon interval
		 *	[2] capability information
		 *	[tlv] ssid
		 *	[tlv] supported rates
		 *	[tlv] country information
		 *	[tlv] parameter set (FH/DS)
		 *	[tlv] erp information
		 *	[tlv] extended supported rates
		 *	[tlv] WPA or RSN
		 */
		IEEE80211_VERIFY_LENGTH(efrm - frm, 12);
		tstamp  = frm;				frm += 8;
		bintval = le16toh(*(u_int16_t *)frm);	frm += 2;
		capinfo = le16toh(*(u_int16_t *)frm);	frm += 2;
		ssid = rates = xrates = country = wpa = NULL;
		bchan = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
		chan = bchan;
		fhdwell = 0;
		fhindex = 0;
		erp = 0;
		timoff = 0;
			ic->ic_stats.is_rx_mgtdiscard++;
		while (frm < efrm) {
			switch (*frm) {
			case IEEE80211_ELEMID_SSID:
				ssid = frm;
				break;
			case IEEE80211_ELEMID_RATES:
				rates = frm;
				break;
			case IEEE80211_ELEMID_COUNTRY:
				country = frm;
				break;
			case IEEE80211_ELEMID_FHPARMS:
				if (ic->ic_phytype == IEEE80211_T_FH) {
					fhdwell = LE_READ_2(&frm[2]);
					chan = IEEE80211_FH_CHAN(frm[4], frm[5]);
					fhindex = frm[6];
				}
				break;
			case IEEE80211_ELEMID_DSPARMS:
				/*
				 * XXX hack this since depending on phytype
				 * is problematic for multi-mode devices.
				 */
				if (ic->ic_phytype != IEEE80211_T_FH)
					chan = frm[2];
				break;
			case IEEE80211_ELEMID_TIM:
				/* XXX ATIM? */
				timoff = frm - skb->data;
				break;
			case IEEE80211_ELEMID_IBSSPARMS:
				break;
			case IEEE80211_ELEMID_XRATES:
				xrates = frm;
				break;
			case IEEE80211_ELEMID_ERP:
				if (frm[1] != 1) {
					IEEE80211_DPRINTF(ic,
						IEEE80211_MSG_ELEMID,
						("%s: invalid ERP element; "
						"length %u, expecting 1\n",
						__func__, frm[1]));
					ic->ic_stats.is_rx_elem_toobig++;
					break;
				}
				erp = frm[2];
				break;
			case IEEE80211_ELEMID_RSN:
				wpa = frm;
				break;
			case IEEE80211_ELEMID_VENDOR:
				if (iswpaoui(frm))
					wpa = frm;
				/* XXX Atheros OUI support */
				break;
			default:
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID,
					("%s: element id %u/len %u ignored\n",
					__func__, *frm, frm[1]));
				ic->ic_stats.is_rx_elem_unknown++;
				break;
			}
			frm += frm[1] + 2;
		}
		IEEE80211_VERIFY_ELEMENT(rates, IEEE80211_RATE_MAXSIZE);
		IEEE80211_VERIFY_ELEMENT(ssid, IEEE80211_NWID_LEN);
		if (
#if IEEE80211_CHAN_MAX < 255
		    chan > IEEE80211_CHAN_MAX ||
#endif
		    isclr(ic->ic_chan_active, chan)) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID,
				("%s: ignore %s with invalid channel %u\n",
				__func__,
				ISPROBE(subtype) ? "probe response" : "beacon",
				chan));
			ic->ic_stats.is_rx_badchan++;
			return;
		}
		if (chan != bchan && ic->ic_phytype != IEEE80211_T_FH) {
			/*
			 * Frame was received on a channel different from the
			 * one indicated in the DS params element id;
			 * silently discard it.
			 *
			 * NB: this can happen due to signal leakage.
			 *     But we should take it for FH phy because
			 *     the rssi value should be correct even for
			 *     different hop pattern in FH.
			 */
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_ELEMID,
				("%s: ignore %s on channel %u marked "
				"for channel %u\n", __func__,
				ISPROBE(subtype) ? "probe response" : "beacon",
				bchan, chan));
			ic->ic_stats.is_rx_chanmismatch++;
			return;
		}

		/*
		 * Station mode, check for state updates.  We
		 * consider only 11g stuff right now.
		 */
		if (ic->ic_opmode == IEEE80211_M_STA && ni->ni_associd != 0) {
			if (ni->ni_erp != erp) {
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_ASSOC,
				    ("erp change from %s: was 0x%x, now 0x%x\n",
				    ether_sprintf(wh->i_addr2),
				    ni->ni_erp, erp));
				if (erp & IEEE80211_ERP_USE_PROTECTION)
					ic->ic_flags |= IEEE80211_F_USEPROT;
				else
					ic->ic_flags &= ~IEEE80211_F_USEPROT;
				ni->ni_erp = erp;
				/* XXX statistic */
			}
			if ((ni->ni_capinfo ^ capinfo) & IEEE80211_CAPINFO_SHORT_SLOTTIME) {
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_ASSOC,
				    ("capabilities change from %s: before 0x%x,"
				     " now 0x%x\n", ether_sprintf(wh->i_addr2),
				     ni->ni_capinfo, capinfo));
				/*
				 * NB: we assume short preamble doesn't
				 *     change dynamically
				 */
				ieee80211_set_shortslottime(ic,
					ic->ic_curmode == IEEE80211_MODE_11A ||
					(ni->ni_capinfo & IEEE80211_CAPINFO_SHORT_SLOTTIME));
				ni->ni_capinfo = capinfo;
				/* XXX statistic */
			}
			return;
		}

		/*
		 * Use mac and channel for lookup so we collect all
		 * potential AP's when scanning.  Otherwise we may
		 * see the same AP on multiple channels and will only
		 * record the last one.  We could filter APs here based
		 * on rssi, etc. but leave that to the end of the scan
		 * so we can keep the selection criteria in one spot.
		 * This may result in a bloat of the scanned AP list but
		 * it shouldn't be too much.
		 */
		ni = ieee80211_find_node_with_channel(ic, wh->i_addr2,
				&ic->ic_channels[chan]);
		if (ni == NULL) {
#ifdef IEEE80211_DEBUG
			if (ieee80211_msg_scan(ic))
				dump_probe_beacon(subtype, 1,
				    wh->i_addr2, chan, bchan, capinfo,
				    bintval, erp, ssid, country);
#endif
			ni = ieee80211_dup_bss(ic, wh->i_addr2);
			if (ni == NULL)
				return;
			ni->ni_esslen = ssid[1];
			memset(ni->ni_essid, 0, sizeof(ni->ni_essid));
			memcpy(ni->ni_essid, ssid + 2, ssid[1]);
		} else if (ssid[1] != 0 &&
		    (ISPROBE(subtype) || ni->ni_esslen == 0)) {
			/*
			 * Update ESSID at probe response to adopt hidden AP by
			 * Lucent/Cisco, which announces null ESSID in beacon.
			 */
			ni->ni_esslen = ssid[1];
			memset(ni->ni_essid, 0, sizeof(ni->ni_essid));
			memcpy(ni->ni_essid, ssid + 2, ssid[1]);
#ifdef IEEE80211_DEBUG
			if (ieee80211_msg_scan(ic) || ieee80211_msg_debug(ic))
				dump_probe_beacon(subtype, 0,
				    wh->i_addr2, chan, bchan, capinfo,
				    bintval, erp, ssid, country);
#endif
		}
		IEEE80211_ADDR_COPY(ni->ni_bssid, wh->i_addr3);
		ni->ni_rssi = rssi;
		ni->ni_rstamp = rstamp;
		memcpy(ni->ni_tstamp.data, tstamp, sizeof(ni->ni_tstamp));
		ni->ni_intval = bintval;
		ni->ni_capinfo = capinfo;
		/* XXX validate channel # */
		ni->ni_chan = &ic->ic_channels[chan];
		ni->ni_fhdwell = fhdwell;
		ni->ni_fhindex = fhindex;
		ni->ni_erp = erp;
		/*
		 * Record the byte offset from the mac header to
		 * the start of the TIM information element for
		 * use by hardware and/or to speedup software
		 * processing of beacon frames.
		 */
		ni->ni_timoff = timoff;
		/*
		 * Record optional information elements that might be
		 * used by applications or drivers.
		 */
		if (wpa != NULL)
			ieee80211_saveie(&ni->ni_wpa_ie, wpa);
		/* NB: must be after ni_chan is setup */
		ieee80211_setup_rates(ic, ni, rates, xrates, IEEE80211_F_DOSORT);
		ieee80211_unref_node(&ni);	/* NB: do not free */
		break;
	}

	case IEEE80211_FC0_SUBTYPE_PROBE_REQ: {
		u_int8_t rate;

		if (ic->ic_opmode == IEEE80211_M_STA ||
		    ic->ic_state != IEEE80211_S_RUN) {
			ic->ic_stats.is_rx_mgtdiscard++;
			return;
		}

		/*
		 * prreq frame format
		 *	[tlv] ssid
		 *	[tlv] supported rates
		 *	[tlv] extended supported rates
		 */
		ssid = rates = xrates = NULL;
		while (frm < efrm) {
			switch (*frm) {
			case IEEE80211_ELEMID_SSID:
				ssid = frm;
				break;
			case IEEE80211_ELEMID_RATES:
				rates = frm;
				break;
			case IEEE80211_ELEMID_XRATES:
				xrates = frm;
				break;
			}
			frm += frm[1] + 2;
		}
		IEEE80211_VERIFY_ELEMENT(rates, IEEE80211_RATE_MAXSIZE);
		IEEE80211_VERIFY_ELEMENT(ssid, IEEE80211_NWID_LEN);
		IEEE80211_VERIFY_SSID(ic->ic_bss, ssid, "probe");

		if (ni == ic->ic_bss) {
			ni = ieee80211_dup_bss(ic, wh->i_addr2);
			if (ni == NULL)
				return;
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_ASSOC,
				("%s: new probe req from %s\n",
				__func__, ether_sprintf(wh->i_addr2)));
			allocbs = 1;
		} else
			allocbs = 0;
		ni->ni_rssi = rssi;
		ni->ni_rstamp = rstamp;
		rate = ieee80211_setup_rates(ic, ni, rates, xrates,
				IEEE80211_F_DOSORT | IEEE80211_F_DOFRATE
				| IEEE80211_F_DONEGO | IEEE80211_F_DODEL);
		if (rate & IEEE80211_RATE_BASIC) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_XRATE,
				("%s: rate negotiation failed: %s\n",
				__func__,ether_sprintf(wh->i_addr2)));
		} else {
			IEEE80211_SEND_MGMT(ic, ni,
				IEEE80211_FC0_SUBTYPE_PROBE_RESP, 0);
		}
		if (allocbs) {
			/*
			 * When operating as an AP we discard the node's
			 * state until the station requests authentication.
			 * This may be better done by holding it and setting
			 * a short timer for reclaiming it but reduces the
			 * possibility of stations flooding us with probe
			 * requests causing our memory use to grow quickly
			 * (though this can still happen if they send
			 * authentication requests).  When operating in ibss
			 * mode we hold the node but with a zero reference
			 * count; this is the current convention (XXX).
			 */
			if (ic->ic_opmode == IEEE80211_M_HOSTAP)
				ieee80211_free_node(ic, ni);
			else
				ieee80211_unref_node(&ni);
		}
		break;
	}

	case IEEE80211_FC0_SUBTYPE_AUTH: {
		u_int16_t algo, seq, status;
		/*
		 * auth frame format
		 *	[2] algorithm
		 *	[2] sequence
		 *	[2] status
		 *	[tlv*] challenge
		 */
		IEEE80211_VERIFY_LENGTH(efrm - frm, 6);
		algo   = le16toh(*(u_int16_t *)frm);
		seq    = le16toh(*(u_int16_t *)(frm + 2));
		status = le16toh(*(u_int16_t *)(frm + 4));
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_AUTH,
			("%s: algorithm %d seq %d from %s\n",
			__func__, algo, seq, ether_sprintf(wh->i_addr2)));

		/*
		 * Consult the ACL policy module if setup.
		 */
		if (ic->ic_acl != NULL &&
		    !ic->ic_acl->iac_check(ic, wh->i_addr2)) {
			IEEE80211_DPRINTF(ic,
			    IEEE80211_MSG_AUTH | IEEE80211_MSG_ACL,
			    ("[%s] reject auth request by station due to ACL\n",
			    ether_sprintf(wh->i_addr2)));
			ic->ic_stats.is_rx_acl++;
			if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
				IEEE80211_SEND_MGMT(ic, ni,
					IEEE80211_FC0_SUBTYPE_AUTH,
					(seq+1) | (IEEE80211_STATUS_ALG<<16));
			}
			return;
		}
		if (ic->ic_flags & IEEE80211_F_COUNTERM) {
			/* XXX only in ap mode? */
			IEEE80211_DPRINTF(ic,
			    IEEE80211_MSG_AUTH | IEEE80211_MSG_CRYPTO,
			    ("[%s] reject auth request by station due to TKIP "
			    "countermeasures\n",
			    ether_sprintf(wh->i_addr2)));
			ic->ic_stats.is_rx_auth_countermeasures++;
			if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
				IEEE80211_SEND_MGMT(ic, ni,
					IEEE80211_FC0_SUBTYPE_AUTH,
					IEEE80211_REASON_MIC_FAILURE);
			}
			return;
		}
		if (algo == IEEE80211_AUTH_ALG_SHARED)
			ieee80211_auth_shared(ic, wh, frm + 6, efrm, ni, rssi,
			    rstamp, seq, status);
		else if (algo == IEEE80211_AUTH_ALG_OPEN)
			ieee80211_auth_open(ic, wh, ni, rssi, rstamp, seq,
			    status);
		else {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
				("%s: unsupported auth algorithm %d from %s\n",
				__func__, algo, ether_sprintf(wh->i_addr2)));
			ic->ic_stats.is_rx_auth_unsupported++;
			if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
				/* XXX not right */
				IEEE80211_SEND_MGMT(ic, ni,
					IEEE80211_FC0_SUBTYPE_AUTH,
					(seq+1) | (IEEE80211_STATUS_ALG<<16));
			}
			return;
		} 
		break;
	}

	case IEEE80211_FC0_SUBTYPE_ASSOC_REQ:
	case IEEE80211_FC0_SUBTYPE_REASSOC_REQ: {
		u_int16_t capinfo, bintval;
		struct ieee80211_rsnparms rsn;
		u_int8_t reason;

		if (ic->ic_opmode != IEEE80211_M_HOSTAP ||
		    ic->ic_state != IEEE80211_S_RUN) {
			ic->ic_stats.is_rx_mgtdiscard++;
			return;
		}

		if (subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ) {
			reassoc = 1;
			resp = IEEE80211_FC0_SUBTYPE_REASSOC_RESP;
		} else {
			reassoc = 0;
			resp = IEEE80211_FC0_SUBTYPE_ASSOC_RESP;
		}
		/*
		 * asreq frame format
		 *	[2] capability information
		 *	[2] listen interval
		 *	[6*] current AP address (reassoc only)
		 *	[tlv] ssid
		 *	[tlv] supported rates
		 *	[tlv] extended supported rates
		 *	[tlv] WPA or RSN
		 */
		IEEE80211_VERIFY_LENGTH(efrm - frm, (reassoc ? 10 : 4));
		if (!IEEE80211_ADDR_EQ(wh->i_addr3, ic->ic_bss->ni_bssid)) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
				("%s: ignore assoc request with bss %s not "
				"our own\n",
				__func__, ether_sprintf(wh->i_addr2)));
			ic->ic_stats.is_rx_assoc_bss++;
			return;
		}
		capinfo = le16toh(*(u_int16_t *)frm);	frm += 2;
		bintval = le16toh(*(u_int16_t *)frm);	frm += 2;
		if (reassoc)
			frm += 6;	/* ignore current AP info */
		ssid = rates = xrates = wpa = NULL;
		while (frm < efrm) {
			switch (*frm) {
			case IEEE80211_ELEMID_SSID:
				ssid = frm;
				break;
			case IEEE80211_ELEMID_RATES:
				rates = frm;
				break;
			case IEEE80211_ELEMID_XRATES:
				xrates = frm;
				break;
			/* XXX verify only one of RSN and WPA ie's? */
			case IEEE80211_ELEMID_RSN:
				wpa = frm;
				break;
			case IEEE80211_ELEMID_VENDOR:
				if (iswpaoui(frm)) {
					if (ic->ic_flags & IEEE80211_F_WPA1)
						wpa = frm;
				}
				/* XXX Atheros OUI support */
				break;
			}
			frm += frm[1] + 2;
		}
		IEEE80211_VERIFY_ELEMENT(rates, IEEE80211_RATE_MAXSIZE);
		IEEE80211_VERIFY_ELEMENT(ssid, IEEE80211_NWID_LEN);
		IEEE80211_VERIFY_SSID(ic->ic_bss, ssid, 
			reassoc ? "reassoc" : "assoc");

		if (ni == ic->ic_bss) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
			    ("[%s] deny %sassoc, not authenticated\n",
			    ether_sprintf(wh->i_addr2), reassoc ? "re" : ""));
			ni = ieee80211_dup_bss(ic, wh->i_addr2);
			if (ni != NULL) {
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_DEAUTH,
				    IEEE80211_REASON_ASSOC_NOT_AUTHED);
				ieee80211_free_node(ic, ni);
			}
			ic->ic_stats.is_rx_assoc_notauth++;
			return;
		}
		if (wpa != NULL) {
			/*
			 * Parse WPA information element.  Note that
			 * we initialize the param block from the node
			 * state so that information in the IE overrides
			 * our defaults.  The resulting parameters are
			 * installed below after the association is assured.
			 */
			rsn = ni->ni_rsn;
			if (wpa[0] != IEEE80211_ELEMID_RSN)
				reason = ieee80211_parse_wpa(ic, wpa, &rsn);
			else
				reason = ieee80211_parse_rsn(ic, wpa, &rsn);
			if (reason != 0) {
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
				    ("%s: bad %s ie from %s, reason %u\n",
				    __func__, wpa[0] != IEEE80211_ELEMID_RSN ?
						"WPA" : "RSN",
				    ether_sprintf(wh->i_addr2), reason));
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_DEAUTH, reason);
				ieee80211_node_leave(ic, ni);
				/* XXX distinguish WPA/RSN? */
				ic->ic_stats.is_rx_assoc_badwpaie++;
				return;
			}
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_ASSOC | IEEE80211_MSG_WPA,
				("%s: %s ie from %s, "
				"mc %u/%u uc %u/%u key %u caps 0x%x\n",
				__func__, wpa[0] != IEEE80211_ELEMID_RSN ?
					"WPA" : "RSN",
				ether_sprintf(wh->i_addr2),
				rsn.rsn_mcastcipher, rsn.rsn_mcastkeylen,
				rsn.rsn_ucastcipher, rsn.rsn_ucastkeylen,
				rsn.rsn_keymgmt, rsn.rsn_caps));
		}
		/* discard challenge after association */
		if (ni->ni_challenge != NULL) {
			FREE(ni->ni_challenge, M_DEVBUF);
			ni->ni_challenge = NULL;
		}
		/* XXX some stations use the privacy bit for handling APs
		       that suport both encrypted and unencrypted traffic */
		/* NB: PRIVACY flag bits are assumed to match */

		if ((capinfo & IEEE80211_CAPINFO_ESS) == 0 ||
		    (capinfo & IEEE80211_CAPINFO_PRIVACY) ^
		    (ic->ic_flags & IEEE80211_F_PRIVACY)) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
				("%s: capability mismatch 0x%x for %s\n",
				__func__, capinfo, ether_sprintf(wh->i_addr2)));
			IEEE80211_SEND_MGMT(ic, ni, resp,
				IEEE80211_STATUS_CAPINFO);
			ieee80211_node_leave(ic, ni);
			ic->ic_stats.is_rx_assoc_capmismatch++;
			return;
		}
		ieee80211_setup_rates(ic, ni, rates, xrates,
				IEEE80211_F_DOSORT | IEEE80211_F_DOFRATE |
				IEEE80211_F_DONEGO | IEEE80211_F_DODEL);
		if (ni->ni_rates.rs_nrates == 0) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
				("%s: rate mismatch for %s\n",
				__func__, ether_sprintf(wh->i_addr2)));
			IEEE80211_SEND_MGMT(ic, ni, resp,
				IEEE80211_STATUS_BASIC_RATE);
			ieee80211_node_leave(ic, ni);
			ic->ic_stats.is_rx_assoc_norate++;
			return;
		}
		ni->ni_rssi = rssi;
		ni->ni_rstamp = rstamp;
		ni->ni_intval = bintval;
		ni->ni_capinfo = capinfo;
		ni->ni_chan = ic->ic_bss->ni_chan;
		ni->ni_fhdwell = ic->ic_bss->ni_fhdwell;
		ni->ni_fhindex = ic->ic_bss->ni_fhindex;
		if (wpa != NULL) {
			/*
			 * Record WPA/RSN parameters for station, mark
			 * node as using WPA and record information element
			 * for applications that require it.
			 */
			ni->ni_rsn = rsn;
			ieee80211_saveie(&ni->ni_wpa_ie, wpa);
		}
		ieee80211_node_join(ic, ni, resp);
		break;
	}

	case IEEE80211_FC0_SUBTYPE_ASSOC_RESP:
	case IEEE80211_FC0_SUBTYPE_REASSOC_RESP: {
		u_int16_t capinfo, associd;
		u_int16_t status;

		if (ic->ic_opmode != IEEE80211_M_STA ||
		    ic->ic_state != IEEE80211_S_ASSOC) {
			ic->ic_stats.is_rx_mgtdiscard++;
			return;
		}

		/*
		 * asresp frame format
		 *	[2] capability information
		 *	[2] status
		 *	[2] association ID
		 *	[tlv] supported rates
		 *	[tlv] extended supported rates
		 */
		IEEE80211_VERIFY_LENGTH(efrm - frm, 6);
		ni = ic->ic_bss;
		capinfo = le16toh(*(u_int16_t *)frm);
		frm += 2;
		status = le16toh(*(u_int16_t *)frm);
		frm += 2;
		if (status != 0) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_ASSOC,
				("%sassociation failed (reason %d) for %s\n",
				ISREASSOC(subtype) ?  "re" : "",
				status, ether_sprintf(wh->i_addr3)));
			if (ni != ic->ic_bss)	/* XXX never true? */
				ni->ni_fails++;
			ic->ic_stats.is_rx_auth_fail++;	/* XXX */
			return;
		}
		associd = le16toh(*(u_int16_t *)frm);
		frm += 2;

		rates = xrates = NULL;
		while (frm < efrm) {
			switch (*frm) {
			case IEEE80211_ELEMID_RATES:
				rates = frm;
				break;
			case IEEE80211_ELEMID_XRATES:
				xrates = frm;
				break;
			}
			frm += frm[1] + 2;
		}

		IEEE80211_VERIFY_ELEMENT(rates, IEEE80211_RATE_MAXSIZE);
		ieee80211_setup_rates(ic, ni, rates, xrates,
				IEEE80211_F_DOSORT | IEEE80211_F_DOFRATE |
				IEEE80211_F_DONEGO | IEEE80211_F_DODEL);
		if (ni->ni_rates.rs_nrates == 0) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_ASSOC,
				("%sassociation failed (rate set mismatch) "
				"for %s\n",
				ISREASSOC(subtype) ?  "re" : "",
				ether_sprintf(wh->i_addr3)));
			if (ni != ic->ic_bss)	/* XXX never true? */
				ni->ni_fails++;
			ic->ic_stats.is_rx_assoc_norate++;
			return;
		}

		ni->ni_capinfo = capinfo;
		ni->ni_associd = associd;
		/*
		 * Configure state now that we are associated.
		 *
		 * XXX may need different/additional driver callbacks?
		 */
		if (ic->ic_curmode == IEEE80211_MODE_11A ||
		    (ni->ni_capinfo & IEEE80211_CAPINFO_SHORT_PREAMBLE)) {
			ic->ic_flags |= IEEE80211_F_SHPREAMBLE;
			ic->ic_flags &= ~IEEE80211_F_USEBARKER;
		} else {
			ic->ic_flags &= ~IEEE80211_F_SHPREAMBLE;
			ic->ic_flags |= IEEE80211_F_USEBARKER;
		}
		ieee80211_set_shortslottime(ic,
			ic->ic_curmode == IEEE80211_MODE_11A ||
			(ni->ni_capinfo & IEEE80211_CAPINFO_SHORT_SLOTTIME));
		/*
		 * Honor ERP protection.
		 *
		 * NB: ni_erp should zero for non-11g operation.
		 * XXX check ic_curmode anyway?
		 */
		if (ni->ni_erp & IEEE80211_ERP_USE_PROTECTION)
			ic->ic_flags |= IEEE80211_F_USEPROT;
		else
			ic->ic_flags &= ~IEEE80211_F_USEPROT;
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ASSOC,
			("%sassociate with %s: %s preamble, %s slot time%s\n",
			ISREASSOC(subtype) ?  "re" : "",
			ether_sprintf(wh->i_addr2),
			ic->ic_flags&IEEE80211_F_SHPREAMBLE ? "short" : "long",
			ic->ic_flags&IEEE80211_F_SHSLOT ? "short" : "long",
			ic->ic_flags&IEEE80211_F_USEPROT ? ", protection" : "")
		);
		ieee80211_new_state(ic, IEEE80211_S_RUN, subtype);
		break;
	}

	case IEEE80211_FC0_SUBTYPE_DEAUTH: {
		u_int16_t reason;

		if (ic->ic_state == IEEE80211_S_SCAN) {
			ic->ic_stats.is_rx_mgtdiscard++;
			return;
		}
		/*
		 * deauth frame format
		 *	[2] reason
		 */
		IEEE80211_VERIFY_LENGTH(efrm - frm, 2);
		reason = le16toh(*(u_int16_t *)frm);
		ic->ic_stats.is_rx_deauth++;
		switch (ic->ic_opmode) {
		case IEEE80211_M_STA:
			ieee80211_new_state(ic, IEEE80211_S_AUTH,
			    wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK);
			break;
		case IEEE80211_M_HOSTAP:
			if (ni != ic->ic_bss) {
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_AUTH,
					("station %s deauthenticated by "
					"peer (reason %d)\n",
					ether_sprintf(ni->ni_macaddr), reason));
				ieee80211_node_leave(ic, ni);
			}
			break;
		default:
			ic->ic_stats.is_rx_mgtdiscard++;
			break;
		}
		break;
	}

	case IEEE80211_FC0_SUBTYPE_DISASSOC: {
		u_int16_t reason;

		if (ic->ic_state != IEEE80211_S_RUN &&
		    ic->ic_state != IEEE80211_S_AUTH) {
			ic->ic_stats.is_rx_mgtdiscard++;
			return;
		}
		/*
		 * disassoc frame format
		 *	[2] reason
		 */
		IEEE80211_VERIFY_LENGTH(efrm - frm, 2);
		reason = le16toh(*(u_int16_t *)frm);
		ic->ic_stats.is_rx_disassoc++;
		switch (ic->ic_opmode) {
		case IEEE80211_M_STA:
			ieee80211_new_state(ic, IEEE80211_S_ASSOC,
			    wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK);
			break;
		case IEEE80211_M_HOSTAP:
			if (ni != ic->ic_bss) {
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_ASSOC,
					("station %s disassociated by "
					"peer (reason %d)\n",
					ether_sprintf(ni->ni_macaddr), reason));
				ieee80211_node_leave(ic, ni);
			}
			break;
		default:
			ic->ic_stats.is_rx_mgtdiscard++;
			break;
		}
		break;
	}
	default:
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
			("%s: mgmt frame with subtype 0x%x not handled\n",
			__func__, subtype));
		ic->ic_stats.is_rx_badsubtype++;
		break;
	}
#undef ISREASSOC
#undef ISPROBE
}
#undef IEEE80211_VERIFY_LENGTH
#undef IEEE80211_VERIFY_ELEMENT

static void
ieee80211_recv_pspoll(struct ieee80211com *ic,
	struct ieee80211_node *ni, struct sk_buff *skb0)
{
	struct ieee80211_frame_min *wh;
	struct sk_buff *skb;
	u_int16_t aid;

	if (ic->ic_set_tim == NULL)	/* No powersaving functionality */
		return;
	if (ni == ic->ic_bss) {
		wh = (struct ieee80211_frame_min *)skb0->data;
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_POWER | IEEE80211_MSG_DEBUG,
			("station %s sent bogus power save poll\n",
			ether_sprintf(wh->i_addr2)));
		return;
	}

	wh = (struct ieee80211_frame_min *)skb0->data;
	memcpy(&aid, wh->i_dur, sizeof(wh->i_dur));
	if ((aid & 0xc000) != 0xc000) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_POWER | IEEE80211_MSG_DEBUG,
			("station %s sent bogus aid %x\n",
			ether_sprintf(wh->i_addr2), aid));
		/* XXX statistic */
		return;
	}
	if (aid != ni->ni_associd) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_POWER | IEEE80211_MSG_DEBUG,
			("station %s aid %x doesn't match pspoll aid %x\n",
			ether_sprintf(wh->i_addr2), ni->ni_associd, aid));
		/* XXX statistic */
		return;
	}

	/* Okay, take the first queued packet and put it out... */
	IF_DEQUEUE(&ni->ni_savedq, skb);
	if (skb == NULL) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_POWER,
			("station %s sent pspoll, but no packets are saved\n",
			ether_sprintf(wh->i_addr2)));
		/* XXX statistic */
		return;
	}
	/* 
	 * If this is the last packet, turn off the TIM fields.
	 * If there are more packets, set the more packets bit.
	 */
	if (_IF_QLEN(&ni->ni_savedq) == 0) {
		if (ic->ic_set_tim) 
			(*ic->ic_set_tim)(ic, ni->ni_associd, 0);
	} else {
		wh = (struct ieee80211_frame_min *) skb->data;
		wh->i_fc[1] |= IEEE80211_FC1_MORE_DATA;
	}
	IEEE80211_DPRINTF(ic, IEEE80211_MSG_POWER,
		("enqueued power saving packet for station %s\n",
		 ether_sprintf(ni->ni_macaddr)));
	/* XXX need different driver interface */
	(*ic->ic_dev->hard_start_xmit)(skb, ic->ic_dev);
}
