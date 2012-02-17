/*-
 * Copyright (c) 2004 Sam Leffler, Errno Consulting
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
 * $Id: ieee80211_dot1x.c,v 1.1.1.1 2009/08/19 08:02:21 jack Exp $
 */
#ifndef EXPORT_SYMTAB
#define	EXPORT_SYMTAB
#endif

/*
 * 802.1x+WPA authenticator protocol handling and state machine.
 *
 * This support is optional; it is only used when the 802.11 layer's
 * authentication mode is set to use 802.1x or WPA is enabled separately
 * (for WPA-PSK).  If compiled as a module this code does not need
 * to be present unless 802.1x/WPA-PSK is in use.
 *
 * The authenticator hooks into the 802.11 layer through callbacks
 * that are invoked when stations join and leave (associate and
 * deassociate).  Authentication state is managed separately from the
 * 802.11 layer's node state.  State is synchronized with a single
 * lock.  This scheme is also used by the optional radius client.
 *
 * It might be possible to generalize the 802.1x support to handle
 * non-802.11 devices but for now it is tightly integrated with the
 * 802.11 code.
 */
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/netfilter.h>
#include <linux/sysctl.h>
#include <linux/in.h>

#include <linux/crypto.h>
#include <asm/scatterlist.h>
#include <linux/random.h>

#include "if_media.h"
#include "if_llc.h"
#include "if_ethersubr.h"

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_dot1x.h>
#include <net80211/ieee80211_radius.h>

#ifndef TRUE
enum { TRUE = 1, FALSE = 0 };		/* for consistency with spec */
#endif

/*
 * Global settings that can be modified with sysctl's.
 */
u_int	eapol_idletimo = 30;		/* 30 seconds */
u_int	eapol_reauthlimit = 5;
u_int	eapol_reauthmin = 60;		/* 60 seconds */
u_int	eapol_reauthtimo = 5*60;	/* 5 minutes */
u_int	eapol_txtimo = 3;		/* 3 seconds */
u_int	eapol_supptimo = 30;		/* 3O seconds */
u_int	eapol_servtimo = 3;		/* 3 seconds */
u_int	eapol_suppreqlimit = 2;
u_int	eapol_servreqlimit = 2;

u_int	eapol_keytxenabled = TRUE;	/* ena/dis EAPOL key trasmission */
u_int	eapol_reauthenabled = TRUE;	/* ena/dis reauthentication */

#ifdef IEEE80211_DEBUG
static const char *eapol_as_states[] = {
	"EAPOL_AS_INIT",
	"EAPOL_AS_DISCONNECTED",
	"EAPOL_AS_CONNECTING",
	"EAPOL_AS_AUTHENTICATING",
	"EAPOL_AS_AUTHENTICATED",
	"EAPOL_AS_ABORTING",
	"EAPOL_AS_HELD",
	"EAPOL_AS_FORCE_AUTH",
	"EAPOL_AS_FORCE_UNAUTH",
};
static const char *eapol_abs_states[] = {
	"EAPOL_ABS_INIT",
	"EAPOL_ABS_IDLE",
	"EAPOL_ABS_REQUEST",
	"EAPOL_ABS_RESPONSE",
	"EAPOL_ABS_SUCCESS",
	"EAPOL_ABS_FAIL",
	"EAPOL_ABS_TIMEOUT",
};
static const char *eapol_aks_states[] = {
	"EAPOL_AKS_NO_KEY",
	"EAPOL_AKS_KEY",
};
static const char *eapol_ars_states[] = {
	"EAPOL_ARS_INIT",
	"EAPOL_ARS_REAUTH",
};
#endif /* IEEE80211_DEBUG */

MALLOC_DEFINE(M_EAPOL_NODE, "node", "802.1x node state");

struct eapolcom *eapolcom = NULL;	/* ``the'' authenticator */
struct eapolstats eapolstats = { 0 };
EXPORT_SYMBOL(eapolstats);

static	struct eapol_auth_node *eapol_new_node(struct eapolcom *,
		struct ieee80211com *, struct ieee80211_node *);
static	void txCannedFail(struct eapol_auth_node *);
static	void txCannedSuccess(struct eapol_auth_node *);
static	void txReqId(struct eapol_auth_node *);
static	void eap_send_simple(struct eapol_node *, u_int8_t code, u_int8_t id);

/*
 * Input handling (messages from the supplicant).
 */

/*
 * Process an EAP frame; we're only interested in
 * EAP Response messages.
 */
static int
eapol_auth_input_eap(struct eapol_auth_node *ean, struct eap_hdr *eap,
	struct sk_buff *skb)
{
	struct eapolcom *ec;

	EAPOL_LOCK_ASSERT(ean->ean_ec);

	if (eap->eap_id != ean->ean_currentId) {
		IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1X,
			("[%s] EAP id mismatch, got %u, expecting %u\n",
			ether_sprintf(ean->ean_node->ni_macaddr),
			eap->eap_id, ean->ean_currentId));
		eapolstats.eas_idmismatch++;
		goto out;
	}
	if (eap->eap_code != EAP_CODE_RESPONSE) {
		/* unexpected response from station */
		IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1X,
			("[%s] unexpected EAP code %u\n",
			ether_sprintf(ean->ean_node->ni_macaddr),
			eap->eap_code));
		eapolstats.eas_badcode++;
		goto out;
	}
	ec = ean->ean_ec;
	if (eap->eap_type == EAP_TYPE_IDENTITY) {
		/*
		 * Record identity string.
		 */
		/* NB: known to be in host byte order */
		if (eap->eap_len > sizeof(struct eap_hdr)+EAP_IDENTITY_MAXLEN) {
			IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1X,
				("[%s] EAP Identity len %u too big\n",
				ether_sprintf(ean->ean_node->ni_macaddr),
				eap->eap_len));
			eapolstats.eas_badidlen++;
			goto out;
		}
		ean->ean_idlen = eap->eap_len - sizeof(struct eap_hdr);
		memcpy(ean->ean_id, &eap[1], ean->ean_idlen);
		ean->ean_id[ean->ean_idlen] = '\0';
		ean->ean_rxRespId = TRUE;
		IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1X,
			("[%s] RECV EAP Identity \"%s\"\n",
			ether_sprintf(ean->ean_node->ni_macaddr),
			ean->ean_id));
		/*
		 * Let radius client reset it's state.
		 */
		if (ec->ec_radius)
			(*ec->ec_radius->rc_identity_input)(ean, skb);
	} else {
		IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1X,
			("[%s] RECV EAP msg type %u len %u\n",
			ether_sprintf(ean->ean_node->ni_macaddr),
			eap->eap_type, eap->eap_len));
		if (eap->eap_type != EAP_TYPE_NAK)
			ean->ean_backendNonNakResponsesForSupplicant++;
		ean->ean_rxResp = TRUE;
	}
	/*
	 * Save the frame contents for later.
	 */
	if (ean->ean_skb)
		kfree_skb(ean->ean_skb);
	ean->ean_skb = skb;

	eapol_fsm_run(ean);
	return 0;
out:
	kfree_skb(skb);
	return 0;
}

/*
 * Process an EAPOL Key frame.
 */
static int
eapol_auth_input_key(struct eapol_auth_node *ean, struct eapol_hdr *eapol,
	struct sk_buff *skb)
{

	EAPOL_LOCK_ASSERT(ean->ean_ec);

	IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1X,
		("[%s] RECV EAPOL WPA-Key\n",
		ether_sprintf(ean->ean_node->ni_macaddr)));

	kfree_skb(skb);
	return 0;
}

/*
 * Receive an EAPOL frame from the device layer.  This
 * routine is installed as a packet handler for EAPOL
 * Ethernet frames when the authenticator is attached.
 */
int
eapol_input(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt)
{
	struct eapolcom *ec;
	struct ieee80211com *ic;
	struct ieee80211_node *ni;
	struct eapol_hdr *eapol;
	struct eapol_auth_node *ean;
	struct eap_hdr *eap;
	int error = 0;

	if (eapolcom == NULL) {
		eapolstats.eps_noauth++;
		goto out;
	}
	ec = eapolcom;
	if (!pskb_may_pull(skb, sizeof(struct eapol_hdr))) {
		eapolstats.eps_tooshort++;
		goto out;
	}
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (skb == NULL) {
		eapolstats.eps_sharecheck++;
		goto out;
	}
	eapol = (struct eapol_hdr *)skb->data;
	(void) skb_pull(skb, sizeof(struct eapol_hdr));
	/* NB: XXX give netfilter a chance? */

	/*
	 * NB: The spec says backwards compatibility for packet
	 *     types is preserved.  So we are safe to accept
	 *     packets with a version number >= to our version
	 *     and still be able to correctly process the packets
	 *     we know/care about.
	 */
	if (eapol->eapol_ver < EAPOL_VERSION) {
		eapolstats.eps_badver++;
		goto out;
	}
	/*
	 * Find session based on sender's MAC address.
	 */
	/* NB: must release reference */
	ic = ieee80211_find_vap(skb->mac.ethernet->h_dest);
	if (ic == NULL) {
		eapolstats.eps_noinstance++;
		goto out;
	}
	ni = ieee80211_find_node(ic, skb->mac.ethernet->h_source);
	if (ni == NULL) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_DOT1X,
		    ("[%s] EAPOL msg type %u with no 802.11 node\n",
		    ether_sprintf(skb->mac.ethernet->h_source),
		    eapol->eapol_type));
		eapolstats.eps_nonode++;
		goto out;
	}
	EAPOL_LOCK(ec);
	ean = eapolcom->ec_table[IEEE80211_NODE_AID(ni)];
	if (ean == NULL && eapol->eapol_type != EAPOL_TYPE_START) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_DOT1X,
		    ("[%s] EAPOL msg type %u with no session\n",
		    ether_sprintf(ni->ni_macaddr), eapol->eapol_type));
		eapolstats.eps_nosession++;
		goto unlock;
	}
	/* put length in host byte order */
	eapol->eapol_len = ntohs(eapol->eapol_len);

	switch (eapol->eapol_type) {
	case EAPOL_TYPE_EAP:
		if (!pskb_may_pull(skb, sizeof(struct eap_hdr_short))) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_DOT1X,
			    ("[%s] EAP msg too short, eapol len %u\n",
			    ether_sprintf(ean->ean_node->ni_macaddr),
			    eapol->eapol_len));
			eapolstats.eap_tooshort++;
			goto unlock;
		}
		eap = (struct eap_hdr *)skb->data;
		eap->eap_len = ntohs(eap->eap_len);
		if (eap->eap_len < eapol->eapol_len) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_DOT1X,
			    ("[%s] EAP length invalid, %u < eapol len %u\n",
			    ether_sprintf(ean->ean_node->ni_macaddr),
			    eap->eap_len, eapol->eapol_len));
			eapolstats.eap_lenmismatch++;
			goto unlock;
		}

		switch (eap->eap_code) {
		case EAP_CODE_RESPONSE:
			if (!pskb_may_pull(skb, sizeof(struct eap_hdr))) {
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_DOT1X,
				    ("[%s] EAP msg too short, len %u\n",
				    ether_sprintf(ean->ean_node->ni_macaddr),
				    skb->len));
				eapolstats.eap_tooshort++;
				goto unlock;
			}
			error = eapol_auth_input_eap(ean, eap, skb);
			skb = NULL;		/* consumed */
			break;
		case EAP_CODE_SUCCESS:
		case EAP_CODE_FAILURE:
		case EAP_CODE_REQUEST:
			/* XXX no need for these */
			/* fall thru... */
		default:
			eapolstats.eap_badcode++;
			goto unlock;
		}
		break;
	case EAPOL_TYPE_START:
		if (ean == NULL) {
			ean = eapol_new_node(eapolcom, ic, ni);
			if (ean == NULL)
				goto unlock;
		}
		IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1X,
			("[%s] RECV EAPOL Start\n",
			ether_sprintf(ean->ean_node->ni_macaddr)));
		ean->ean_eapStart = TRUE;
		eapol_fsm_run(ean);
		break;
	case EAPOL_TYPE_LOGOFF:
		IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1X,
			("[%s] RECV EAPOL Logoff\n",
			ether_sprintf(ean->ean_node->ni_macaddr)));
		ean->ean_eapLogoff = TRUE;
		eapol_fsm_run(ean);
		break;
	case EAPOL_TYPE_KEY:
		if (!pskb_may_pull(skb, sizeof(struct eapol_wpa_key))) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_DOT1X,
			    ("[%s] EAPOL KEY msg too short, eapol len %u\n",
			    ether_sprintf(ean->ean_node->ni_macaddr),
			    eapol->eapol_len));
			eapolstats.eap_keytooshort++;
			goto unlock;
		}
		error = eapol_auth_input_key(ean, eapol, skb);
		skb = NULL;		/* consumed */
		eapol_fsm_run(ean);
		break;
	case EAPOL_TYPE_EASFA:
		/* XXX not handled */
		/* fall thru... */
	default:
		eapolstats.eps_badtype++;
		goto unlock;
	}
unlock:
	/* NB: unref is safe 'cuz ean holds a ref */
	ieee80211_unref_node(&ni);
	EAPOL_UNLOCK(ec);
out:
	if (skb != NULL)
		kfree_skb(skb);
	return error;
}

/*
 * 802.1x+WPA state machine support.  We follow the 802.1x and
 * WPA specs by defining independent state machines for the
 * supplicant-authenticator, authenticator-backend, reauthentication
 * timer, and key transmit handling.  These machines are ``clocked''
 * together when various events take place (receipt of EAPOL messages
 * from the supplicant, radius messages from the backened, timers, etc.)
 */

#define	STATE_DECL(type,state) \
	static void eapol_auth_##type##_##state(struct eapol_auth_node *ean)
#define	STATE_ENTER(type,state, ean)	eapol_auth_##type##_##state(ean)

/*
 * Authenticator state machine.
 */
#define	AS_STATE_DECL(s)	STATE_DECL(AS,s)
#define	AS_STATE_ENTER(s,ean)	STATE_ENTER(AS,s,ean)
#ifdef IEEE80211_DEBUG
#define	AS_STATE_DEBUG(s,ean)					\
	IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1XSM,	\
		("[%s] %s -> %s\n",				\
		ether_sprintf(ean->ean_node->ni_macaddr),	\
		eapol_as_states[ean->ean_authState],		\
		eapol_as_states[EAPOL_AS_##s]))
#define	AS_STATE_OPT_DEBUG(s,ean) do {				\
	if (ean->ean_authState != EAPOL_AS_##s)			\
		AS_STATE_DEBUG(s,ean);				\
} while (0)
#else
#define	AS_STATE_DEBUG(s,ean)
#define	AS_STATE_OPT_DEBUG(s,ean)
#endif

AS_STATE_DECL(INIT)
{
	struct eapolcom *ec = ean->ean_ec;

	AS_STATE_OPT_DEBUG(INIT, ean);

	(*ec->ec_node_reset)(ean);	/* reset non-fsm state */
	ean->ean_currentId = 0;
	ean->ean_portMode = EAPOL_PORTMODE_AUTO;
	ean->ean_authState = EAPOL_AS_INIT;
}

AS_STATE_DECL(DISCONNECTED)
{
	AS_STATE_DEBUG(DISCONNECTED, ean);

	ean->ean_portStatus = EAPOL_PORTSTATUS_UNAUTH;
	ieee80211_node_unauthorize(ean->ean_ic, ean->ean_node);

	ean->ean_eapLogoff = FALSE;
	ean->ean_reAuthCount = 0;
	if (ean->ean_authState != EAPOL_AS_INIT) {
		txCannedFail(ean);
		ean->ean_currentId++;
	}
	ean->ean_authState = EAPOL_AS_DISCONNECTED;
	/* XXX remove station keys */
}

AS_STATE_DECL(HELD)
{

	AS_STATE_DEBUG(HELD, ean);

	ean->ean_portStatus = EAPOL_PORTSTATUS_UNAUTH;
	ieee80211_node_unauthorize(ean->ean_ic, ean->ean_node);

	ean->ean_quietWhile = eapol_idletimo;
	ean->ean_eapLogoff = FALSE;
	ean->ean_currentId++;
	ean->ean_authState = EAPOL_AS_HELD;
}

AS_STATE_DECL(CONNECTING)
{

	AS_STATE_DEBUG(CONNECTING, ean);

	ean->ean_eapStart = FALSE;
	ean->ean_reAuthenticate = FALSE;
	ean->ean_txWhen = eapol_txtimo;
	ean->ean_rxRespId = FALSE;
	txReqId(ean);
	ean->ean_reAuthCount++;
	ean->ean_authState = EAPOL_AS_CONNECTING;
}

AS_STATE_DECL(AUTHENTICATED)
{
	AS_STATE_DEBUG(AUTHENTICATED, ean);

	ean->ean_portStatus = EAPOL_PORTSTATUS_AUTH;
	ieee80211_node_authorize(ean->ean_ic, ean->ean_node);

	ean->ean_reAuthCount = 0;
	ean->ean_currentId++;
	ean->ean_authState = EAPOL_AS_AUTHENTICATED;
}

AS_STATE_DECL(AUTHENTICATING)
{
	AS_STATE_DEBUG(AUTHENTICATING, ean);

	ean->ean_authSuccess = FALSE;
	ean->ean_authFail = FALSE;
	ean->ean_authTimeout = FALSE;
	ean->ean_authStart = TRUE;
	ean->ean_authState = EAPOL_AS_AUTHENTICATING;
}

AS_STATE_DECL(ABORTING)
{
	AS_STATE_DEBUG(ABORTING, ean);

	ean->ean_currentId++;
	ean->ean_authAbort = TRUE;
	ean->ean_authState = EAPOL_AS_ABORTING;
}

AS_STATE_DECL(FORCE_AUTH)
{
	AS_STATE_DEBUG(FORCE_AUTH, ean);

	ean->ean_portStatus = EAPOL_PORTSTATUS_AUTH;
	ieee80211_node_authorize(ean->ean_ic, ean->ean_node);

	ean->ean_portMode = EAPOL_PORTMODE_FORCEAUTH;
	ean->ean_eapStart = FALSE;
	txCannedSuccess(ean);
	ean->ean_currentId++;
	ean->ean_authState = EAPOL_AS_FORCE_AUTH;
}

AS_STATE_DECL(FORCE_UNAUTH)
{
	AS_STATE_DEBUG(FORCE_UNAUTH, ean);

	ean->ean_portStatus = EAPOL_PORTSTATUS_UNAUTH;
	ieee80211_node_unauthorize(ean->ean_ic, ean->ean_node);

	ean->ean_portMode = EAPOL_PORTMODE_FORCEUNAUTH;
	ean->ean_eapStart = FALSE;
	txCannedFail(ean);
	ean->ean_currentId++;
	ean->ean_authState = EAPOL_AS_FORCE_UNAUTH;
}

/*
 * Carry out a state transition in the authentication
 * state machine. This routine encapsulates Figure 8-8
 * in the spec.
 */
static void
eapol_auth_step(struct eapol_auth_node *ean)
{

	EAPOL_LOCK_ASSERT(ean->ean_ec);

	/* NB: portEnabled is implicit */
	if ((ean->ean_portControl == EAPOL_PORTCONTROL_AUTO &&
	    ean->ean_portMode != ean->ean_portControl) ||
	    ean->ean_initialize) {
		AS_STATE_ENTER(INIT, ean);
		return;
	}
	switch (ean->ean_authState) {
	case EAPOL_AS_INIT:
		AS_STATE_ENTER(DISCONNECTED, ean);
		break;
	case EAPOL_AS_DISCONNECTED:
		AS_STATE_ENTER(CONNECTING, ean);
		break;
	case EAPOL_AS_HELD:
		if (ean->ean_quietWhile == 0)
			AS_STATE_ENTER(CONNECTING, ean);
		break;
	case EAPOL_AS_CONNECTING:
		if (ean->ean_reAuthCount <= eapol_reauthlimit) {
			if (ean->ean_rxRespId) {
				ean->ean_txWhen = 0;
				ean->ean_authEntersAuthenticating++;
				AS_STATE_ENTER(AUTHENTICATING, ean);
			} else if (ean->ean_txWhen == 0 || ean->ean_eapStart ||
			    ean->ean_reAuthenticate) {
				AS_STATE_ENTER(CONNECTING, ean);
			} else if (ean->ean_eapLogoff) {
				AS_STATE_ENTER(DISCONNECTED, ean);
			}
		} else {
			/*
			 * Forcibly deauthenticate station and reset
			 * state.  The spec says we go back to a
			 * DISCONNECTED state but the station is no
			 * longer associated at this point so we do
			 * not maintain state.
			 */
			IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1XSM,
				("[%s] reauth limit reached, deauth station\n",
				ether_sprintf(ean->ean_node->ni_macaddr)));
			IEEE80211_SEND_MGMT(ean->ean_ic, ean->ean_node,
				IEEE80211_FC0_SUBTYPE_DEAUTH,
				IEEE80211_REASON_AUTH_EXPIRE);
			ean->ean_gone = TRUE;
		}
		break;
	case EAPOL_AS_AUTHENTICATED:
		if (ean->ean_eapStart || ean->ean_reAuthenticate) {
			if (ean->ean_eapStart)
				ean->ean_authAuthEapStartsWhileAuthenticated++;
			if (ean->ean_reAuthenticate)
				ean->ean_authAuthReauthsWhileAuthenticated++;
			AS_STATE_ENTER(CONNECTING, ean);
		} else if (ean->ean_eapLogoff) {
			ean->ean_authAuthEapLogoffWhileAuthenticated++;
			AS_STATE_ENTER(DISCONNECTED, ean);
		}
		break;
	case EAPOL_AS_AUTHENTICATING:
		if (ean->ean_authSuccess) {
			ean->ean_authAuthSuccessesWhileAuthenticating++;
			AS_STATE_ENTER(AUTHENTICATED, ean);
		} else if (ean->ean_authFail) {
			ean->ean_authAuthFailWhileAuthenticating++;
			AS_STATE_ENTER(HELD, ean);
		} else if (ean->ean_reAuthenticate || ean->ean_eapStart ||
		    ean->ean_eapLogoff || ean->ean_authTimeout) {
			if (ean->ean_reAuthenticate)
				ean->ean_authAuthReauthsWhileAuthenticating++;
			if (ean->ean_eapStart)
				ean->ean_authAuthEapStartsWhileAuthenticating++;
			if (ean->ean_eapLogoff)
				ean->ean_authAuthEapLogoffWhileAuthenticating++;
			if (ean->ean_authTimeout)
				ean->ean_authAuthTimeoutsWhileAuthenticating++;
			AS_STATE_ENTER(ABORTING, ean);
		}
		break;
	case EAPOL_AS_ABORTING:
		if (ean->ean_eapLogoff && !ean->ean_authAbort)
			AS_STATE_ENTER(DISCONNECTED, ean);
		else if (!ean->ean_eapLogoff && !ean->ean_authAbort)
			AS_STATE_ENTER(CONNECTING, ean);
		break;
	case EAPOL_AS_FORCE_AUTH:
		if (ean->ean_eapStart)
			AS_STATE_ENTER(FORCE_AUTH, ean);
		break;
	case EAPOL_AS_FORCE_UNAUTH:
		if (ean->ean_eapStart)
			AS_STATE_ENTER(FORCE_UNAUTH, ean);
		break;
	default:
		eapolstats.eps_badauthfsm++;
		break;
	}
}
#undef AS_STATE_DECL
#undef AS_STATE_ENTER
#undef AS_STATE_DEBUG

/*
 * Backend state machine.
 */
#define	ABS_STATE_DECL(s)	STATE_DECL(ABS,s)
#define	ABS_STATE_ENTER(s,ean)	STATE_ENTER(ABS,s,ean)
#ifdef IEEE80211_DEBUG
#define	ABS_STATE_DEBUG(s,ean)					\
	IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1XSM,	\
		("[%s] %s -> %s\n",				\
		ether_sprintf(ean->ean_node->ni_macaddr),	\
		eapol_abs_states[ean->ean_backendState],	\
		eapol_abs_states[EAPOL_ABS_##s]))
#define	ABS_STATE_OPT_DEBUG(s,ean) do {				\
	if (ean->ean_backendState != EAPOL_ABS_##s)		\
		ABS_STATE_DEBUG(s,ean);				\
} while (0)
#else
#define	ABS_STATE_DEBUG(s,ean)
#define	ABS_STATE_OPT_DEBUG(s,ean)
#endif

ABS_STATE_DECL(INIT)
{
	ABS_STATE_OPT_DEBUG(INIT, ean);

	/* abort authentication XXX */
	ean->ean_aWhile = 0;
	ean->ean_authAbort = FALSE;
	ean->ean_keyAvailable = FALSE;		/* XXX key fsm */
	ean->ean_backendState = EAPOL_ABS_INIT;
}

ABS_STATE_DECL(IDLE)
{
	ABS_STATE_OPT_DEBUG(IDLE, ean);

	ean->ean_authStart = FALSE;	/* XXX */
	ean->ean_reqCount = 0;
	ean->ean_reqSrvCount = 0;
	ean->ean_backendState = EAPOL_ABS_IDLE;
}

ABS_STATE_DECL(REQUEST)
{
#define	txReq(ean)		(*ean->ean_ec->ec_radius->rc_txreq)(ean)
	ABS_STATE_DEBUG(REQUEST, ean);

	ean->ean_currentId = ean->ean_idFromServer;
	txReq(ean);
	ean->ean_aWhile = eapol_supptimo;
	ean->ean_reqCount++;
	ean->ean_reqSrvCount = 0;
	ean->ean_backendState = EAPOL_ABS_REQUEST;
#undef txReq
}

ABS_STATE_DECL(RESPONSE)
{
#define	sendRespToServer(ean)	(*ean->ean_ec->ec_radius->rc_sendsrvr)(ean)
	ABS_STATE_DEBUG(RESPONSE, ean);

	ean->ean_aReq = FALSE;
	ean->ean_aSuccess = FALSE;
	ean->ean_authTimeout = FALSE;
	ean->ean_rxResp = FALSE;
	ean->ean_aFail = FALSE;
	ean->ean_aWhile = eapol_servtimo;
	ean->ean_reqCount = 0;
	sendRespToServer(ean);
	ean->ean_reqSrvCount++;
	ean->ean_backendState = EAPOL_ABS_RESPONSE;
#undef sendRespToServer
}

ABS_STATE_DECL(SUCCESS)
{
	ABS_STATE_DEBUG(SUCCESS, ean);

	ean->ean_aWhile = 0;
	ean->ean_currentId = ean->ean_idFromServer;
	txCannedSuccess(ean);
	ean->ean_authSuccess = TRUE;
	ean->ean_backendState = EAPOL_ABS_SUCCESS;
}

ABS_STATE_DECL(FAIL)
{
	ABS_STATE_DEBUG(FAIL, ean);

	ean->ean_aWhile = 0;
	ean->ean_currentId = ean->ean_idFromServer;
	txCannedFail(ean);
	ean->ean_authFail = TRUE;
	ean->ean_backendState = EAPOL_ABS_FAIL;
}

ABS_STATE_DECL(TIMEOUT)
{
	ABS_STATE_DEBUG(TIMEOUT, ean);

	if (ean->ean_portStatus == EAPOL_PORTSTATUS_UNAUTH)
		txCannedFail(ean);
	ean->ean_authTimeout = TRUE;
	ean->ean_backendState = EAPOL_ABS_TIMEOUT;
}

/*
 * Carry out a state transition in the backend state machine.
 * This routine encapsulates Figure 8-12 in the spec.
 */
static void
eapol_backend_step(struct eapol_auth_node *ean)
{

	EAPOL_LOCK_ASSERT(ean->ean_ec);

	if (ean->ean_portControl != EAPOL_PORTCONTROL_AUTO ||
	    ean->ean_initialize || ean->ean_authAbort) {
		ABS_STATE_ENTER(INIT, ean);
		return;
	}
	switch (ean->ean_backendState) {
	case EAPOL_ABS_INIT:
		ABS_STATE_ENTER(IDLE, ean);
		break;
	case EAPOL_ABS_IDLE:
		if (ean->ean_authStart)
			ABS_STATE_ENTER(RESPONSE, ean);
		break;
	case EAPOL_ABS_REQUEST:
		if (ean->ean_rxResp)
			ABS_STATE_ENTER(RESPONSE, ean);
		else if (ean->ean_aWhile == 0) {
			if (ean->ean_reqCount < eapol_suppreqlimit)
				ABS_STATE_ENTER(REQUEST, ean);
			else
				ABS_STATE_ENTER(TIMEOUT, ean);
		}
		break;
	case EAPOL_ABS_RESPONSE:
		if (ean->ean_aReq)
			ABS_STATE_ENTER(REQUEST, ean);
		else if (ean->ean_aFail)
			ABS_STATE_ENTER(FAIL, ean);
		else if (ean->ean_aSuccess)
			ABS_STATE_ENTER(SUCCESS, ean);
		else if (ean->ean_aWhile == 0) {
			if (ean->ean_reqSrvCount < eapol_servreqlimit)
				ABS_STATE_ENTER(RESPONSE, ean);
			else
				ABS_STATE_ENTER(TIMEOUT, ean);
		}
		break;
	case EAPOL_ABS_SUCCESS:
	case EAPOL_ABS_FAIL:
	case EAPOL_ABS_TIMEOUT:
		ABS_STATE_ENTER(IDLE, ean);
		break;
	default:
		eapolstats.eps_badbackendfsm++;
		break;
	}
}
#undef ABS_STATE_DECL
#undef ABS_STATE_ENTER
#undef ABS_STATE_DEBUG

/*
 * Reauthentication timer state machine.
 */
#define	REAUTH_STATE_DECL(s)		STATE_DECL(ARS,s)
#define	REAUTH_STATE_ENTER(s,ean)	STATE_ENTER(ARS,s,ean)
#ifdef IEEE80211_DEBUG
#define	REAUTH_STATE_DEBUG(s,ean)				\
	IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1XSM,	\
		("[%s] %s -> %s\n",				\
		ether_sprintf(ean->ean_node->ni_macaddr),	\
		eapol_ars_states[ean->ean_reAuthState],		\
		eapol_ars_states[EAPOL_ARS_##s]))
#define	REAUTH_STATE_OPT_DEBUG(s,ean) do {			\
	if (ean->ean_reAuthState != EAPOL_ARS_##s)		\
		REAUTH_STATE_DEBUG(s,ean);			\
} while (0)
#else
#define	REAUTH_STATE_DEBUG(s,ean)
#define	REAUTH_STATE_OPT_DEBUG(s,ean)
#endif

REAUTH_STATE_DECL(INIT)
{
	REAUTH_STATE_OPT_DEBUG(INIT, ean);

	ean->ean_reAuthWhen = eapol_reauthtimo;
	ean->ean_reAuthState = EAPOL_ARS_INIT;
}

REAUTH_STATE_DECL(REAUTH)
{
	REAUTH_STATE_DEBUG(REAUTH, ean);

	ean->ean_reAuthenticate = TRUE;
	ean->ean_reAuthState = EAPOL_ARS_REAUTH;
}

/*
 * Carry out a state transition in the reauthentication
 * timer state machine. This routine encapsulates
 * Figure 8-11 in the spec.
 */
static void
eapol_reauth_step(struct eapol_auth_node *ean)
{

	EAPOL_LOCK_ASSERT(ean->ean_ec);

	if (ean->ean_portControl != EAPOL_PORTCONTROL_AUTO ||
	    ean->ean_initialize ||
	    ean->ean_portStatus == EAPOL_PORTSTATUS_UNAUTH ||
	    !eapol_reauthenabled) {
		REAUTH_STATE_ENTER(INIT, ean);
		return;
	}
	switch (ean->ean_reAuthState) {
	case EAPOL_ARS_INIT:
		if (ean->ean_reAuthWhen == 0)
			REAUTH_STATE_ENTER(REAUTH, ean);
		break;
	case EAPOL_ARS_REAUTH:
		REAUTH_STATE_ENTER(INIT, ean);
		break;
	default:
		eapolstats.eps_badreauthfsm++;
		break;
	}
}
#undef REAUTH_STATE_DECL
#undef REAUTH_STATE_ENTER
#undef REAUTH_STATE_DEBUG

/*
 * Key transmit state machine.
 */
#define	KEY_STATE_DECL(s)	STATE_DECL(AKS,s)
#define	KEY_STATE_ENTER(s,ean)	STATE_ENTER(AKS,s,ean)
#ifdef IEEE80211_DEBUG
#define	KEY_STATE_DEBUG(s,ean)					\
	IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1XSM,	\
		("[%s] %s -> %s\n",				\
		ether_sprintf(ean->ean_node->ni_macaddr),	\
		eapol_aks_states[ean->ean_keyState],		\
		eapol_aks_states[EAPOL_AKS_##s]))
#define	KEY_STATE_OPT_DEBUG(s,ean) do {				\
	if (ean->ean_keyState != EAPOL_AKS_##s)			\
		KEY_STATE_DEBUG(s,ean);				\
} while (0)
#else
#define	KEY_STATE_DEBUG(s,ean)
#define	KEY_STATE_OPT_DEBUG(s,ean)
#endif

KEY_STATE_DECL(NO_KEY)
{
	KEY_STATE_OPT_DEBUG(NO_KEY, ean);

	ean->ean_keyState = EAPOL_AKS_NO_KEY;
}

KEY_STATE_DECL(KEY)
{
#define	txKey(ean)	(*ean->ean_ec->ec_radius->rc_txkey)(ean)
	KEY_STATE_DEBUG(KEY, ean);

	txKey(ean);
	ean->ean_keyAvailable = FALSE;
	ean->ean_keyState = EAPOL_AKS_KEY;
#undef txKey
}

/*
 * Carry out a state transition in the key transmit
 * state machine. This routine encapsulates
 * Figure 8-9 in the spec.
 */
static void
eapol_key_step(struct eapol_auth_node *ean)
{

	EAPOL_LOCK_ASSERT(ean->ean_ec);

	if (ean->ean_portControl != EAPOL_PORTCONTROL_AUTO ||
	    ean->ean_initialize) {
		KEY_STATE_ENTER(NO_KEY, ean);
		return;
	}
	switch (ean->ean_keyState) {
	case EAPOL_AKS_NO_KEY:
		if (eapol_keytxenabled && ean->ean_keyAvailable)
			KEY_STATE_ENTER(KEY, ean);
		break;
	case EAPOL_AKS_KEY:
		if (!eapol_keytxenabled ||
		    ean->ean_authFail || ean->ean_eapLogoff)
			KEY_STATE_ENTER(NO_KEY, ean);
		else if (ean->ean_keyAvailable)
			KEY_STATE_ENTER(KEY, ean);
		break;
	default:
		eapolstats.eps_badkeyfsm++;
		break;
	}
}
#undef KEY_STATE_DECL
#undef KEY_STATE_ENTER
#undef KEY_STATE_DEBUG

/*
 * Run the state machines so long as there are state transitions.
 */
void
eapol_fsm_run(struct eapol_auth_node *ean)
{
	int ostate, statechange;

	do {
		statechange = FALSE;

		ostate = ean->ean_authState;
		eapol_auth_step(ean);
		if (ean->ean_gone) {
			struct ieee80211com *ic = ean->ean_ic;
			struct ieee80211_node *ni = ean->ean_node;

			/*
			 * Delayed handling of node reclamation when the
			 * reauthentication timer expires.  We must be
			 * careful as the call to ieee80211_node_leave
			 * will reclaim our state so we cannot reference
			 * it past that point.  However this happens the
			 * caller must also be careful to not reference state.
			 */
			ieee80211_node_leave(ic, ni);
			ic->ic_stats.is_node_timeout++;
			return;
		}
		statechange |= ostate != ean->ean_authState;

		ostate = ean->ean_backendState;
		eapol_backend_step(ean);
		statechange |= ostate != ean->ean_backendState;

		ostate = ean->ean_keyState;
		eapol_key_step(ean);
		statechange |= ostate != ean->ean_keyState;

		ostate = ean->ean_reAuthState;
		eapol_reauth_step(ean);
		statechange |= ostate != ean->ean_reAuthState;
	} while (statechange);
}
EXPORT_SYMBOL(eapol_fsm_run);

/*
 * Process timers for an authenticator's nodes.
 */
static void
eapol_check_timers(unsigned long arg)
{
	struct eapolcom *ec = (struct eapolcom *)arg;
	struct eapol_auth_node *ean;
	int i, inuse, timeout;

	/*
	 * NB: this is very simplistic, but if the table is large
	 * the time spent here may be noticeable and we may want
	 * to optimize the work.
	 */
	EAPOL_LOCK(ec);
	inuse = ec->ec_inuse;
	for (i = 0; i < ec->ec_maxaid && inuse; i++) {
		ean = ec->ec_table[i];
		if (ean == NULL)
			continue;
		timeout = FALSE;
		if (ean->ean_aWhile && --ean->ean_aWhile == 0) {
			IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1X,
				("[%s] TIMEOUT aWhile, %s\n",
				ether_sprintf(ean->ean_node->ni_macaddr),
				eapol_abs_states[ean->ean_backendState]));
			timeout = TRUE;
		}
		if (ean->ean_quietWhile && --ean->ean_quietWhile == 0) {
			IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1X,
				("[%s] TIMEOUT quietWhile, %s\n",
				ether_sprintf(ean->ean_node->ni_macaddr),
				eapol_as_states[ean->ean_authState]));
			timeout = TRUE;
		}
		if (ean->ean_reAuthWhen && --ean->ean_reAuthWhen == 0) {
			IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1X,
				("[%s] TIMEOUT reAuthWhen, %s\n",
				ether_sprintf(ean->ean_node->ni_macaddr),
				eapol_ars_states[ean->ean_reAuthState]));
			timeout = TRUE;
		}
		if (ean->ean_txWhen && --ean->ean_txWhen == 0) {
			IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1X,
				("[%s] TIMEOUT txWhen, %s\n",
				ether_sprintf(ean->ean_node->ni_macaddr),
				eapol_as_states[ean->ean_authState]));
			timeout = TRUE;
		}
		if (timeout)
			eapol_fsm_run(ean);
		inuse--;
	}
	if (ec->ec_inuse) {
		ec->ec_timer.expires = jiffies + HZ;	/* once 1 second */
		add_timer(&ec->ec_timer);
	}
	EAPOL_UNLOCK(ec);
}

/*
 * Output routines to send EAP frames to the supplicant.
 */

/*
 * Send an EAP Failure packet to the supplicant.
 */
static void
txCannedFail(struct eapol_auth_node *ean)
{
	IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1X,
		("[%s] txCannedFail currentId %u\n",
		ether_sprintf(ean->ean_node->ni_macaddr),
		ean->ean_currentId));
	eap_send_simple(&ean->ean_base, EAP_CODE_FAILURE, ean->ean_currentId);
}

/*
 * Send an EAP Success packet to the supplicant.
 */
static void
txCannedSuccess(struct eapol_auth_node *ean)
{
	IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1X,
		("[%s] txCannedSuccess currentId %u\n",
		ether_sprintf(ean->ean_node->ni_macaddr),
		ean->ean_currentId));
	eap_send_simple(&ean->ean_base, EAP_CODE_SUCCESS, ean->ean_currentId);
}

void
eapol_hmac_md5(struct eapolcom *ec, void *data, u_int datalen,
	void *key, u_int keylen, u_int8_t hash[16])
{
	struct scatterlist sg;

	sg.page = virt_to_page(data);
	sg.offset = offset_in_page(data);
	sg.length = datalen;

	crypto_hmac(ec->ec_md5, key, &keylen, &sg, 1, hash);
}
EXPORT_SYMBOL(eapol_hmac_md5);

/*
 * Allocate a buffer large enough to hold the specified
 * payload preceded by an EAPOL header and the other
 * headers required to send the result as an 802.11 frame.
 */
struct sk_buff *
eapol_alloc_skb(u_int payload)
{
	/* NB: these will never be encrypted */
	const int overhead =
		  sizeof(struct ieee80211_qosframe)
		+ sizeof(struct llc)
		+ sizeof(struct ether_header)
		+ sizeof(struct eapol_hdr)
		;
	struct sk_buff *skb;

	skb = dev_alloc_skb(overhead + payload);
	if (skb != NULL)
		skb_reserve(skb, overhead);
	else
		eapolstats.eps_nobuf++;
	return skb;
}
EXPORT_SYMBOL(eapol_alloc_skb);

/*
 * Send an EAP Request/Identity packet to the supplicant.
 */
static void
txReqId(struct eapol_auth_node *ean)
{
	struct eap_hdr *eap;
	struct sk_buff *skb;

	IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1X,
		("[%s] %s currentId %u\n",
		ether_sprintf(ean->ean_node->ni_macaddr), __func__,
		ean->ean_currentId));
	skb = eapol_alloc_skb(sizeof(struct eap_hdr));
	if (skb == NULL) {
		IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_ANY,
			("[%s] could not allocate sk_buff (%s) \n",
			ether_sprintf(ean->ean_node->ni_macaddr), __func__));
		return;
	}
	eap = (struct eap_hdr *)skb_put(skb, sizeof(struct eap_hdr));
	eap->eap_code = EAP_CODE_REQUEST;
	eap->eap_id = ean->ean_currentId;
	eap->eap_len = htons(sizeof(struct eap_hdr));
	eap->eap_type = EAP_TYPE_IDENTITY;

	eapol_send(&ean->ean_base, skb, EAPOL_TYPE_EAP);

	ean->ean_txWhen = eapol_txtimo;
}

/*
 * Set the re-authentication timer based on a setting
 * received from the backend server.  We sanity check
 * it and install it in the node for use if/when the
 * reauthentication timer is started.
 */
void
eapol_reauth_setperiod(struct eapol_auth_node *ean, int timeout)
{

	if (timeout == 0)
		ean->ean_reAuthPeriod = eapol_reauthtimo;
	else if (timeout < eapol_reauthmin)
		ean->ean_reAuthPeriod = eapol_reauthmin;
	else
		ean->ean_reAuthPeriod = timeout;
	IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_DOT1X,
		("[%s] reauthentication period set to %u secs\n",
		ether_sprintf(ean->ean_node->ni_macaddr),
		ean->ean_reAuthPeriod));
}
EXPORT_SYMBOL(eapol_reauth_setperiod);

/*
 * Node management support.
 */

/*
 * Node management callbacks.  All of these are
 * subclassed when the radius client is used.
 */

/*
 * Allocate a node on station join/discovery.
 */
static struct eapol_auth_node *
eapol_node_alloc(struct eapolcom *ec)
{
	struct eapol_auth_node *ean;

	MALLOC(ean, struct eapol_auth_node *, sizeof(struct eapol_auth_node),
		M_EAPOL_NODE, M_NOWAIT | M_ZERO);
	if (ean == NULL)
		eapolstats.eps_nonodemem++;
	return ean;
}

/*
 * Reclaim a node on station leave or shutdown.
 */
static void
eapol_node_free(struct eapol_auth_node *ean)
{
	FREE(ean, M_EAPOL_NODE);
}

/*
 * Reset an existing node's non-state machine
 * state when a station reassociates.
 */
static void
eapol_node_reset(struct eapol_auth_node *ean)
{
	if (ean->ean_skb != NULL) {
		kfree_skb(ean->ean_skb);
		ean->ean_skb = NULL;
	}
	ean->ean_gone = FALSE;

	/* reset timers, aWhile is reset by backend fsm */
	ean->ean_quietWhile = 0;
	ean->ean_reAuthWhen = 0;
	ean->ean_txWhen = 0;
	ean->ean_reKeyWhen = 0;
	ean->ean_gReKeyWhen = 0;

	ean->ean_portControl = EAPOL_PORTCONTROL_AUTO;
}

/*
 * Create an entry for a client.
 */
static struct eapol_auth_node *
eapol_new_node(struct eapolcom *ec,
	struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct eapol_auth_node *ean;

	/*
	 * Allocate and initialize the node state.
	 */
	ean = (*ec->ec_node_alloc)(ec);
	if (ean == NULL) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
			("[%s] no memory for EAPOL node\n",
			ether_sprintf(ni->ni_macaddr)));
		return NULL;
	}
	ean->ean_ec = ec;
	ean->ean_ic = ic;
	ean->ean_node = ni;

	/*
	 * Add the new entry to the table.
	 */
	EAPOL_LOCK(ec);
	KASSERT(ec->ec_table[IEEE80211_NODE_AID(ni)] == NULL,
	    ("entry already present for association id %u!",
	    IEEE80211_NODE_AID(ni)));
	ec->ec_table[IEEE80211_NODE_AID(ni)] = ean;
	if (ec->ec_inuse++ == 0) {
		/* start timer on first node */
		ec->ec_timer.expires = jiffies + HZ;
		add_timer(&ec->ec_timer);
	}
	EAPOL_UNLOCK(ec);

	return ean;
}

/*
 * Create an entry in response to a station associating
 * reassociating.  This also triggers the state machine
 * which results in the supplicant receiving a message.
 */
static void
eapol_node_join(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct eapolcom *ec = ic->ic_ec;
	struct eapol_auth_node *ean;

	IEEE80211_DPRINTF(ic, IEEE80211_MSG_DOT1X,
		("[%s] 802.1x node join\n", ether_sprintf(ni->ni_macaddr)));
	ean = ec->ec_table[IEEE80211_NODE_AID(ni)];
	if (ean == NULL) {
		/*
		 * Bump the reference count so long as we hold a reference.
		 */
		ieee80211_ref_node(ni);
		ean = eapol_new_node(ec, ic, ni);
		if (ean == NULL) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_DOT1X,
				("[%s] 802.1x node join FAILED\n",
				ether_sprintf(ni->ni_macaddr)));
			ieee80211_unref_node(&ni);
			return;
		}
	}
	/*
	 * Start/Restart state machine.  Note that we crank
	 * the state machine(s) twice because the initialize
	 * handling will prematurely terminate stepping.
	 * This should be safe to do.
	 */
	EAPOL_LOCK(ec);
	ean->ean_initialize = TRUE;
	eapol_fsm_run(ean);
	ean->ean_initialize = FALSE;
#if 0
	eapol_fsm_run(ean);
#else
	/* XXX delay initial Identity request by 1 second */
	ean->ean_txWhen = 1;
#endif
	EAPOL_UNLOCK(ec);
}

/* 
 * Remove a node from the table and reclaim its resources.
 */
static void
eapol_node_leave(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct eapolcom *ec = ic->ic_ec;
	struct eapol_auth_node *ean;

	EAPOL_LOCK(ec);
	ean = ec->ec_table[IEEE80211_NODE_AID(ni)];
	if (ean == NULL) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_DOT1X,
			("[%s] 802.1x node leave, but not found, aid %u\n",
			ether_sprintf(ni->ni_macaddr), IEEE80211_NODE_AID(ni)));
		/* XXX statistic */
	} else {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_DOT1X,
			("[%s] 802.1x node leave (%s)\n",
			ether_sprintf(ni->ni_macaddr),
			eapol_as_states[ean->ean_authState]));
		/* 
		 * Remove the entry from the table.
		 */
		ec->ec_table[IEEE80211_NODE_AID(ni)] = NULL;
		if (--ec->ec_inuse == 0) {
			/* clear timer on last node */
			del_timer(&ec->ec_timer);
		}
		EAPOL_UNLOCK(ec);
		/*
		 * Delete the reference to the node and free our memory.
		 */
		ieee80211_free_node(ean->ean_ic, ean->ean_node);
		if (ean->ean_skb)
			kfree_skb(ean->ean_skb);
		(*ec->ec_node_free)(ean);
	}
}

/* 
 * Remove all nodes from the table and reclaim their resources.
 */
static void
eapol_delete_all_nodes(struct eapolcom *ec)
{
	struct eapol_auth_node *ean;
	int i;

	EAPOL_LOCK(ec);
	for (i = 0; i < ec->ec_maxaid; i++) {
		ean = ec->ec_table[i];
		if (ean) {
			ec->ec_table[i] = NULL;
			ieee80211_free_node(ean->ean_ic, ean->ean_node);
			if (ean->ean_skb)
				kfree_skb(ean->ean_skb);
			(*ec->ec_node_free)(ean);
		}
	}
	ec->ec_inuse = 0;
	del_timer(&ec->ec_timer);	/* clear timer */
	EAPOL_UNLOCK(ec);
}

/*
 * Send a formulated EAPOL packet.
 */
void
eapol_send_raw(struct eapol_node *en, struct sk_buff *skb)
{
	struct net_device *dev = en->en_ic->ic_dev;

	/*
	 * Fill the device header for the EAPOL frame
	 */
	skb->dev = dev;
	skb->protocol = __constant_htons(ETH_P_EAPOL);
	skb->mac.raw = skb->nh.raw = skb->data;
	if (dev->hard_header &&
	    dev->hard_header(skb, dev, ETH_P_EAPOL, en->en_node->ni_macaddr, dev->dev_addr, skb->len) < 0)
		goto out;

	/* Send it off, maybe filter it using firewalling first.  */
	NF_HOOK(NF_EAPOL, NF_EAPOL_OUT, skb, NULL, dev, dev_queue_xmit);
	return;
out:
	kfree_skb(skb);
}
EXPORT_SYMBOL(eapol_send_raw);

/*
 * Create and send an EAPOL packet.
 */
void
eapol_send(struct eapol_node *en, struct sk_buff *skb, u_int8_t type)
{
	struct eapol_hdr *eapol;

	/*
	 * Craft the EAPOL header.
	 */
	eapol = (struct eapol_hdr *)skb_push(skb, sizeof(*eapol));
	eapol->eapol_ver = EAPOL_VERSION;
	eapol->eapol_type = type;
	eapol->eapol_len = htons(skb->len - sizeof(struct eapol_hdr));

	eapol_send_raw(en, skb);
}
EXPORT_SYMBOL(eapol_send);

#if 0
/*
 * Create and send a simple EAPOL packet.
 */
void
eapol_send_simple(struct eapol_node *en, u_int8_t type)
{
	struct sk_buff *skb;

	skb = eapol_alloc_skb(0);
	if (skb == NULL) {
		IEEE80211_DPRINTF(en->en_ic, IEEE80211_MSG_ANY,
			("[%s] could not allocate sk_buff (%s) \n",
			ether_sprintf(en->en_node->ni_macaddr), __func__));
		return;
	}
	eapol_send(en, skb, type);
}
#endif

/*
 * Create and send a simple EAP packet.
 */
static void
eap_send_simple(struct eapol_node *en, u_int8_t code, u_int8_t id)
{
	struct eap_hdr_short *eap;
	struct sk_buff *skb;

	skb = eapol_alloc_skb(sizeof(struct eap_hdr_short));
	if (skb == NULL) {
		IEEE80211_DPRINTF(en->en_ic, IEEE80211_MSG_ANY,
			("[%s] could not allocate sk_buff (%s) \n",
			ether_sprintf(en->en_node->ni_macaddr), __func__));
		return;
	}
	eap = (struct eap_hdr_short *)
		skb_put(skb, sizeof(struct eap_hdr_short));
	eap->eap_code = code;
	eap->eap_id = id;
	eap->eap_len = __constant_htons(sizeof(struct eap_hdr_short));

	eapol_send(en, skb, EAPOL_TYPE_EAP);
}

static void
eapol_cleanup(struct eapolcom *ec)
{
	EAPOL_LOCK_DESTROY(ec);
	if (ec->ec_md5 != NULL)
		crypto_free_tfm(ec->ec_md5);
	if (ec->ec_table != NULL)
		FREE(ec->ec_table, M_DEVBUF);
	FREE(ec, M_DEVBUF);
}

static struct eapolcom *
eapol_setup(void)
{
	struct eapolcom *ec;
	struct crypto_tfm *tfm;

	/* XXX WAITOK? */
	MALLOC(ec, struct eapolcom *, sizeof(*ec), M_DEVBUF, M_WAITOK | M_ZERO);
	if (ec == NULL) {
		printk("%s: no memory for state block!\n", __func__);
		return NULL;
	}
	EAPOL_LOCK_INIT(ec, "eapol");

	tfm = crypto_alloc_tfm("md5", 0);
	if (tfm == NULL) {
		/* XXX fallback on internal implementation? */
		printf("%s: unable to allocate md5 crypto state\n", __func__);
		/* XXX statistic */
		goto bad;
	}
	ec->ec_md5 = tfm;

	ec->ec_node_alloc = eapol_node_alloc;
	ec->ec_node_free = eapol_node_free;
	ec->ec_node_reset = eapol_node_reset;

	return ec;
bad:
	eapol_cleanup(ec);
	return NULL;
}

static struct packet_type eapol_packet_type = {
	.type =	__constant_htons(ETH_P_EAPOL),
	.func =	eapol_input,
};

/*
 * Attach an authenticator.
 */
static int
eapol_authenticator_attach(struct ieee80211com *ic)
{
	struct eapolcom *ec;

	_MOD_INC_USE(THIS_MODULE, return FALSE);

	if (eapolcom == NULL) {
		/* XXX cannot happen */
		printk(KERN_ERR "%s: no eapolcom!\n", __func__);
		_MOD_DEC_USE(THIS_MODULE);
		return FALSE;
	}
	ec = eapolcom;

	ec->ec_maxaid = ic->ic_max_aid;
	MALLOC(ec->ec_table, struct eapol_auth_node **,
		sizeof(struct eapol_auth_node *) * ic->ic_max_aid,
		M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ec->ec_table == NULL) {
		printk("%s: no memory for aid table!\n", __func__);
		_MOD_DEC_USE(THIS_MODULE);
		return FALSE;
	}

	/*
	 * Startup radius client.
	 */
	ec->ec_backend = ieee80211_authenticator_backend_get("radius");
	if (ec->ec_backend == NULL ||
	    !ec->ec_backend->iab_attach(ec))
		goto bad;

	init_timer(&ec->ec_timer);
	ec->ec_timer.data = (unsigned long) ec;
	ec->ec_timer.function = eapol_check_timers;

	/*
	 * Bind to the 802.11 layer.
	 */
	ic->ic_ec = ec;

	dev_add_pack(&eapol_packet_type);
	printk(KERN_INFO "802.1x authenticator started\n");
	return TRUE;
bad:
	FREE(ec->ec_table, M_DEVBUF);
	ec->ec_table = NULL;
	_MOD_DEC_USE(THIS_MODULE);
	return FALSE;
}

/*
 * Detach an authenticator.
 */
static void
eapol_authenticator_detach(struct ieee80211com *ic)
{
	struct eapolcom *ec = ic->ic_ec;

	if (ec != NULL) {
		dev_remove_pack(&eapol_packet_type);
		/*
		 * Detach from 802.11 layer.
		 */
		ic->ic_ec = NULL;

		/*
		 * NB: must do this before detaching radius support
		 *     as node management methods are reset on detach.
		 */
		eapol_delete_all_nodes(ec);
		if (ec->ec_radius != NULL)
			ec->ec_backend->iab_detach(ec);
		if (ec->ec_table != NULL) {
			FREE(ec->ec_table, M_DEVBUF);
			ec->ec_table = NULL;
		}
		printk(KERN_INFO "802.1x authenticator stopped\n");
	}
	_MOD_DEC_USE(THIS_MODULE);
}

#define	CTL_AUTO	-2	/* cannot be CTL_ANY or CTL_NONE */

/* XXX validate/minmax settings */
static ctl_table dot1x_sysctls[] = {
	{ .ctl_name	= CTL_AUTO,
	  .procname	= "reauthenabled",
	  .data		= &eapol_reauthenabled,
	  .maxlen	= sizeof(eapol_reauthenabled),
	  .mode		= 0644,
	  .proc_handler	= proc_dointvec
	},
	{ .ctl_name	= CTL_AUTO,
	  .procname	= "keytxenabled",
	  .data		= &eapol_keytxenabled,
	  .maxlen	= sizeof(eapol_keytxenabled),
	  .mode		= 0644,
	  .proc_handler	= proc_dointvec
	},
	{ .ctl_name	= CTL_AUTO,
	  .procname	= "idletimo",
	  .data		= &eapol_idletimo,
	  .maxlen	= sizeof(eapol_idletimo),
	  .mode		= 0644,
	  .proc_handler	= proc_dointvec
	},
	{ .ctl_name	= CTL_AUTO,
	  .procname	= "reauthlimit",
	  .data		= &eapol_reauthlimit,
	  .maxlen	= sizeof(eapol_reauthlimit),
	  .mode		= 0644,
	  .proc_handler	= proc_dointvec
	},
	{ .ctl_name	= CTL_AUTO,
	  .procname	= "reauthmin",
	  .data		= &eapol_reauthmin,
	  .maxlen	= sizeof(eapol_reauthmin),
	  .mode		= 0644,
	  .proc_handler	= proc_dointvec
	},
	{ .ctl_name	= CTL_AUTO,
	  .procname	= "reauthtimo",
	  .data		= &eapol_reauthtimo,
	  .maxlen	= sizeof(eapol_reauthtimo),
	  .mode		= 0644,
	  .proc_handler	= proc_dointvec
	},
	{ .ctl_name	= CTL_AUTO,
	  .procname	= "txtimo",
	  .data		= &eapol_txtimo,
	  .maxlen	= sizeof(eapol_txtimo),
	  .mode		= 0644,
	  .proc_handler	= proc_dointvec
	},
	{ .ctl_name	= CTL_AUTO,
	  .procname	= "supptimo",
	  .data		= &eapol_supptimo,
	  .maxlen	= sizeof(eapol_supptimo),
	  .mode		= 0644,
	  .proc_handler	= proc_dointvec
	},
	{ .ctl_name	= CTL_AUTO,
	  .procname	= "servtimo",
	  .data		= &eapol_servtimo,
	  .maxlen	= sizeof(eapol_servtimo),
	  .mode		= 0644,
	  .proc_handler	= proc_dointvec
	},
	{ .ctl_name	= CTL_AUTO,
	  .procname	= "suppreqlimit",
	  .data		= &eapol_suppreqlimit,
	  .maxlen	= sizeof(eapol_suppreqlimit),
	  .mode		= 0644,
	  .proc_handler	= proc_dointvec
	},
	{ .ctl_name	= CTL_AUTO,
	  .procname	= "servreqlimit",
	  .data		= &eapol_servreqlimit,
	  .maxlen	= sizeof(eapol_servreqlimit),
	  .mode		= 0644,
	  .proc_handler	= proc_dointvec
	},
	{ 0 }
};
static ctl_table dot1x_table[] = {
	{ .ctl_name	= NET_8021X,
	  .procname	= "8021x",
	  .mode		= 0555,
	  .child	= dot1x_sysctls
	}, { 0 }
};
static ctl_table net_table[] = {
#ifdef CONFIG_PROC_FS
	{ .ctl_name	= CTL_NET,
	  .procname	= "net",
	  .mode		= 0555,
	  .child	= dot1x_table
	},
#endif /* CONFIG_PROC_FS */
	{ 0 }
};

static struct ctl_table_header *eapol_sysctls;

/*
 * Module glue.
 */

MODULE_AUTHOR("Errno Consulting, Sam Leffler");
MODULE_DESCRIPTION("802.11 wireless support: 802.1x authenticator");
#ifdef MODULE_LICENSE
MODULE_LICENSE("Dual BSD/GPL");
#endif

/*
 * One module handles everything for now.  May want
 * to split things up for embedded applications.
 */
static const struct ieee80211_authenticator dot1x = {
	.ia_name	= "802.1x",
	.ia_attach	= eapol_authenticator_attach,
	.ia_detach	= eapol_authenticator_detach,
	.ia_node_join	= eapol_node_join,
	.ia_node_leave	= eapol_node_leave,
};

static int __init
init_ieee80211_auth(void)
{
	eapolcom = eapol_setup();
	if (eapolcom == NULL)
		return -1;		/* XXX?? */
	eapol_sysctls = register_sysctl_table(net_table, 0);
	ieee80211_authenticator_register(IEEE80211_AUTH_8021X, &dot1x);
	return 0;
}
module_init(init_ieee80211_auth);

static void __exit
exit_ieee80211_auth(void)
{
	if (eapolcom != NULL)
		eapol_cleanup(eapolcom);
	if (eapol_sysctls)
		unregister_sysctl_table(eapol_sysctls);
	ieee80211_authenticator_unregister(IEEE80211_AUTH_8021X);
}
module_exit(exit_ieee80211_auth);
