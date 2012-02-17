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
 * $Id: ieee80211_radius.c,v 1.1.1.1 2009/08/19 08:02:21 jack Exp $
 */
#ifndef EXPORT_SYMTAB
#define	EXPORT_SYMTAB
#endif

/*
 * Radius client support for the 802.1x+WPA authenticator.
 *
 * This support is optional (it is not present when the authenticator
 * uses only pre-shared keys).  We override the authenticator's node
 * management methods to allocate additional space for the radius client
 * and supply methods for the 802.1x backend state machine to communicate
 * with a radius server.  This module creates one thread for receiving
 * messages from the radius server.  All other communication happens
 * asynchronously through callbacks from the 802.1x authenticator code.
 * Likewise we communicate state changes to the authenticator by setting
 * the state variables in a node's state block and then running the
 * authenticator state machine for that node.
 *
 * To use this support the radius server state must be configured before
 * enabling 802.1x authentication.  Specifically you must set the radius
 * server's IP address and port and the shared secret used to communicate.
 * These are all set using sysctl's (or through /proc).  To change any
 * of these settings while the client is running the device must be marked
 * down and up again, or similar (e.g. change authentication mode).
 */
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/sysctl.h>
#include <linux/in.h>
#include <linux/utsname.h>

#include <asm/uaccess.h>

#include <linux/crypto.h>
#include <asm/scatterlist.h>
#include <asm/uaccess.h>
#include <linux/random.h>

#include <net/sock.h>			/* XXX for sk_allocation/allocation */

#include "if_media.h"
#include "if_ethersubr.h"		/* for ETHER_MAX_LEN */
#include "if_llc.h"			/* for LLC_SNAPFRAMELEN */

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_dot1x.h>
#include <net80211/ieee80211_radius.h>

#include "rc4.h"		/* XXX */
#define	arc4_ctxlen()			sizeof (struct rc4_state)
#define	arc4_setkey(_c,_k,_l)		rc4_init(_c,_k,_l)
#define	arc4_encrypt(_c,_d,_s,_l)	rc4_crypt_skip(_c,_s,_d,_l,0)

struct auth_hdr {
	u_int8_t	ah_code;
	u_int8_t	ah_id;
	u_int16_t	ah_len;
	u_int8_t	ah_auth[16];	/* MD5 hash */
	/* variable length attributes follow */
} __attribute__((__packed__));

enum {
	RAD_ACCESS_REQUEST	= 1,
	RAD_ACCESS_ACCEPT	= 2,
	RAD_ACCESS_REJECT	= 3,
	RAD_ACCESS_CHALLENGE	= 11,
};
enum {
	RAD_ATTR_USER_NAME		= 1,
	RAD_ATTR_USER_PASSWD		= 2,
	RAD_ATTR_NAS_IP_ADDRESS		= 4,
	RAD_ATTR_NAS_PORT		= 5,
	RAD_ATTR_SERVICE_TYPE		= 6,
#define	RAD_SERVICE_TYPE_FRAMED		2
	RAD_ATTR_FRAMED_MTU		= 12,
	RAD_ATTR_REPLY_MESSAGE		= 18,
	RAD_ATTR_STATE			= 24,
	RAD_ATTR_CLASS			= 25,
	RAD_ATTR_VENDOR_SPECIFIC	= 26,
	RAD_ATTR_SESSION_TIMEOUT	= 27,
	RAD_ATTR_CALLED_STATION_ID	= 30,
	RAD_ATTR_CALLING_STATION_ID	= 31,
	RAD_ATTR_NAS_IDENTIFIER		= 32,
	RAD_ATTR_NAS_PORT_TYPE		= 61,
#define	RAD_NAS_PORT_TYPE_WIRELESS	19	/* IEEE 802.11 */
	RAD_ATTR_CONNECT_INFO		= 77,
	RAD_ATTR_EAP_MESSAGE		= 79,
	RAD_ATTR_MESSAGE_AUTHENTICATOR	= 80,
	RAD_ATTR_NAS_PORT_ID		= 87,
};

#define	RAD_MAX_ATTR_LEN	(255 - 2)
#define	RAD_MAX_SECRET		48	/* NB: must be a multiple of 16 */

#ifndef TRUE
enum { TRUE = 1, FALSE = 0 };		/* for consistency with spec */
#endif

static	struct sockaddr_in radius_serveraddr;	/* IP address of radius server */
static	struct sockaddr_in radius_clientaddr;	/* IP address of client */
static	u_int8_t radius_secret[RAD_MAX_SECRET+1];/* backend shared secret */
static	u_int	radius_secretlen = 0;

static	int radius_check_auth(struct radiuscom *,
		struct eapol_auth_radius_node *, struct auth_hdr *);
static	int radius_check_msg_auth(struct radiuscom *,
		struct eapol_auth_radius_node *, struct auth_hdr *);
static	struct sk_buff *radius_create_reply(struct radiuscom *,
		struct auth_hdr *, struct eapol_auth_node *);
static	int radius_extract_key(struct radiuscom *,
		struct eapol_auth_radius_node *, struct auth_hdr *);
static	void radius_dumppkt(const char *tag, const struct eapol_auth_node *,
		const struct auth_hdr *);
static	u_int8_t * radius_find_attr(struct auth_hdr *, int attr);
static	void sendRespToServer(struct eapol_auth_node *);
static	struct eapol_auth_radius_node *radius_get_reply(struct radiuscom *,
		u_int8_t id);
static	void radius_add_reply(struct radiuscom *,
		struct eapol_auth_radius_node *);
static	void radius_cleanup(struct radiuscom *);

#ifdef IEEE80211_DEBUG
static	void radius_dumpbytes(const char *tag, const void *data, u_int len);
#endif

static int
radius_get_integer(u_int8_t *ap)
{
	u_int32_t v;

	if (ap[1] >= 2+sizeof(v))
		memcpy(&v, ap+2, sizeof(v));
	else
		v = 0;
	return ntohl(v);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

#include <linux/smp_lock.h>

static void
_daemonize(const char *procname)
{
	lock_kernel();

	daemonize();
	current->tty = NULL;
	strcpy(current->comm, procname);

	unlock_kernel();
}

static int
allow_signal(int sig)
{
	if (sig < 1 || sig > _NSIG)
		return -EINVAL;

#ifdef INIT_SIGHAND
	/*
	 * "The test on INIT_SIGHAND is not perfect but will at
	 *  least allow this to compile on RedHat kernels."
	 *
	 * http://www.mail-archive.com/jfs-discussion@www-124.ibm.com/msg00803.html
	 */
	spin_lock_irq(&current->sighand->siglock);
	sigdelset(&current->blocked, sig);
	/* XXX current->mm? */
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
#else
	spin_lock_irq(&current->sigmask_lock);
	sigdelset(&current->blocked, sig);
	/* XXX current->mm? */
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
#endif

	return 0;
}
#else
#define	_daemonize(s)	daemonize(s)
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0) */

/*
 * Thread to handle responses from the radius server.
 * This just listens for packets, decodes them, and
 * applies their contents to the appropriate session
 * state, then kicks the state machine as necessary.
 */
static int
radiusd(void *arg)
{
	struct radiuscom *rc = arg;
	struct eapolcom *ec = rc->rc_ec;
	mm_segment_t oldfs;
	struct auth_hdr *ahp;
	struct msghdr msg;
	struct iovec iov;
	struct sockaddr_in sin;
	struct eapol_auth_node *ean;
	struct eapol_auth_radius_node *ern;
	u_int8_t *ap;
	int len;

	/* XXX return what */
	_MOD_INC_USE(THIS_MODULE, return -1);

	_daemonize("kradiusd");
	allow_signal(SIGKILL);

	for (;;) {
		msg.msg_name = &sin;
		msg.msg_namelen = sizeof(sin);
		iov.iov_base = rc->rc_buf;
		iov.iov_len = sizeof(rc->rc_buf);
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_flags = 0;
		/*
		 * Gag, override task limits to tell sock_recvmsg & co
		 * that our recv buffer is in kernel space and not user!
		 */
		oldfs = get_fs();
		set_fs(KERNEL_DS);
		len = sock_recvmsg(rc->rc_sock, &msg, iov.iov_len, 0);
		set_fs(oldfs);
		/*
		 * We receive a SIGKILL to detach; ignore any
		 * message that might have been received.
		 */
		if (signal_pending(current))
			break;
		if (len <= 0) {
			printf("%s: sock_recvmsg returns %d\n", __func__, len);
			continue;
		}
		/*
		 * Warn about msgs not from the server (note
		 * this is not meant to be security measure...).
		 */
		if (msg.msg_namelen != sizeof(sin) ||
		    sin.sin_addr.s_addr != rc->rc_server.sin_addr.s_addr) {
			/*
			 * NB: some versions of Linux apparently
			 * do not return the sender's address in
			 * which case we'll want to conditionally
			 * disable this complaint.
			 */
			sin.sin_addr.s_addr = ntohl(sin.sin_addr.s_addr);
			printf("%s: Warning, bad sender address, "
			    "len %u addr %u.%u.%u.%u\n",
			    __func__, msg.msg_namelen,
			    (sin.sin_addr.s_addr>>24)&0xff,
			    (sin.sin_addr.s_addr>>16)&0xff,
			    (sin.sin_addr.s_addr>> 8)&0xff,
			    (sin.sin_addr.s_addr>> 0)&0xff);
			eapolstats.rs_addrmismatch++;
			/* fall thru... */
		}
		/*
		 * Process the radius packet.
		 */
		if (len < sizeof(struct auth_hdr)) {
			printf("%s: msg too short, len %u, discarded\n",
				__func__, len);
			eapolstats.rs_tooshort++;
			continue;
		}
		ahp = (struct auth_hdr *)rc->rc_buf;
		if (ahp->ah_code == RAD_ACCESS_REQUEST) {
			printf("%s: bad code %u, msg discarded\n",
				__func__, ahp->ah_code);
			eapolstats.rs_request++;
			continue;
		}
		/*
		 * NB: many places we assume the packet contents
		 *     are not altered so don't do "optimizations"
		 *     like fixing the byte order of ah_len.
		 */
		if (ntohs(ahp->ah_len) > len) {
			printf("%s: auth len %u > msg len %u, msg discarded\n",
				__func__, ahp->ah_len, len);
			eapolstats.rs_badlen++;
			continue;
		}
		/*
		 * We use the EAPOL state lock to synchronize our
		 * work (which should not take long to complete).
		 */
		EAPOL_LOCK(ec);
		/*
		 * Check the radius server response against the
		 * list of outstanding requests.  Only one should
		 * have a matching id.
		 */
		ern = radius_get_reply(rc, ahp->ah_id);
		if (ern == NULL) {
			EAPOL_UNLOCK(ec);
			printf("%s: RADIUS msg id %u has no pending request\n",
				__func__, ahp->ah_id);
			eapolstats.rs_badid++;
			continue;
		}
		ean = &ern->ern_base;
#ifdef IEEE80211_DEBUG
		if (ieee80211_msg_dumpradius(ean->ean_ic))
			radius_dumppkt("RECV", ean, ahp);
#endif
		/*
		 * Check the response authenticator.
		 */
		if (!radius_check_auth(rc, ern, ahp)) {
			EAPOL_UNLOCK(ec);
			IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_RADIUS,
				("[%s] BAD response authenticator\n",
				ether_sprintf(ean->ean_node->ni_macaddr)));
			eapolstats.rs_badrespauth++;
			/* XXX put ean back on reply list? */
			continue;
		}
		if (ahp->ah_code != RAD_ACCESS_REJECT) {
			/*
			 * Check the Message-Authenticator hash
			 * before trusting the message contents.
			 */
			if (!radius_check_msg_auth(rc, ern, ahp)) {
				EAPOL_UNLOCK(ec);
				IEEE80211_DPRINTF(ean->ean_ic,
				    IEEE80211_MSG_RADIUS,
				    ("[%s] BAD message-auth hash\n",
				    ether_sprintf(ean->ean_node->ni_macaddr)));
				eapolstats.rs_badmsgauth++;
				continue;
			}
			ap = radius_find_attr(ahp, RAD_ATTR_STATE);
			if (ap != NULL) {
				ern->ern_statelen = ap[1] - 2;
				memcpy(ern->ern_state, ap+2, ern->ern_statelen);
			} else
				ern->ern_statelen = 0;
			ap = radius_find_attr(ahp, RAD_ATTR_CLASS);
			if (ap != NULL) {
				ern->ern_classlen = ap[1] - 2;
				memcpy(ern->ern_class, ap+2, ern->ern_classlen);
			} else
				ern->ern_classlen = 0;
			/*
			 * Construct EAPOL reply frame based on the
			 * contents of the radius message just received.
			 * radius_create_reply will only return a reply
			 * buffer if the radius message has at least one EAP
			 * message in it; this is used below.
			 */
			if (ern->ern_msg != NULL)
				kfree_skb(ern->ern_msg);
			ern->ern_msg = radius_create_reply(rc, ahp, ean);
		} else
			ern->ern_msg = NULL;

		switch (ahp->ah_code) {
		case RAD_ACCESS_ACCEPT:
			if (ern->ern_msg == NULL) {
				IEEE80211_DPRINTF(ean->ean_ic,
				    IEEE80211_MSG_RADIUS,
				    ("[%s] RECV RAD_ACCESS_ACCEPT, no EAP\n",
				    ether_sprintf(ean->ean_node->ni_macaddr)));
				ean->ean_backendAuthFails++;
				break;
			}
			IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_RADIUS,
				("[%s] RECV RAD_ACCESS_ACCEPT, success\n",
				ether_sprintf(ean->ean_node->ni_macaddr)));
			/* 
			 * Setup the re-authentication timer based on
			 * any supplied timeout.  Any value we supply
			 * from the packet is sanity-checked before use.
			 */
			ap = radius_find_attr(ahp, RAD_ATTR_SESSION_TIMEOUT);
			eapol_reauth_setperiod(ean, ap ?
				radius_get_integer(ap) : 0);
			/*
			 * Collect key state from the message.
			 */
			if (radius_extract_key(rc, ern, ahp)) {
				IEEE80211_DPRINTF(ean->ean_ic,
				    IEEE80211_MSG_RADIUS,
				    ("[%s] KEYS available\n",
				    ether_sprintf(ean->ean_node->ni_macaddr)));
				ean->ean_keyAvailable = TRUE;
			}
			ean->ean_aFail = FALSE;
			ean->ean_aSuccess = TRUE;
			ean->ean_backendAuthSuccesses++;
			eapol_fsm_run(ean);
			break;
		case RAD_ACCESS_REJECT:
			IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_RADIUS,
				("[%s] RECV RAD_ACCESS_REJECT\n",
				ether_sprintf(ean->ean_node->ni_macaddr)));
			ean->ean_aFail = TRUE;
			ean->ean_aSuccess = FALSE;
			ean->ean_backendAuthFails++;
			/* NB: it's ok for a reject to omit EAP */
			if (ern->ern_msg != NULL)
				ean->ean_currentId = ean->ean_idFromServer;
			eapol_fsm_run(ean);
			break;
		case RAD_ACCESS_CHALLENGE:
			if (ern->ern_msg == NULL) {
				IEEE80211_DPRINTF(ean->ean_ic,
				    IEEE80211_MSG_RADIUS,
				    ("[%s] RECV RAD_ACCESS_CHALLENGE, no EAP\n",
				    ether_sprintf(ean->ean_node->ni_macaddr)));
				/* XXX */
				break;
			}
			IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_RADIUS,
				("[%s] RECV RAD_ACCESS_CHALLENGE, ok\n",
				ether_sprintf(ean->ean_node->ni_macaddr)));
			ean->ean_currentId = ean->ean_idFromServer;
			ean->ean_aReq = TRUE;
			ean->ean_backendAccessChallenges++;
			eapol_fsm_run(ean);
			break;
		default:
			IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_RADIUS,
				("[%s] RECV RADIUS msg code %u ignored\n",
				ether_sprintf(ean->ean_node->ni_macaddr),
				ahp->ah_code));
			if (ern->ern_msg != NULL) {
				kfree_skb(ern->ern_msg);
				ern->ern_msg = NULL;
			}
			eapolstats.rs_badcode++;
			break;
		}
		EAPOL_UNLOCK(ec);
	}
	/*
	 * ieee80211_radius_detach leaves the final
	 * cleanup work to us to avoid race conditions.
	 */
	radius_cleanup(rc);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	module_put_and_exit(0);
#else
	return 0;
#endif
}

/*
 * Write-around for bogus crypto API; requiring the address
 * address be specified twice can easily cause mistakes.
 */
static __inline void
digest_update(struct crypto_tfm *md5, void *data, u_int len)
{
	struct scatterlist sg;

	sg.page = virt_to_page(data);
	sg.offset = offset_in_page(data);
	sg.length = len;
	crypto_digest_update(md5, &sg, 1);
}

/*
 * Verify the authenticator hash in each reply using
 * the shared secret and the hash calculated for the
 * associated request.  The frame contents are assumed
 * to have been checked that they have a valid length.
 */
static int
radius_check_auth(struct radiuscom *rc, struct eapol_auth_radius_node *ern,
	struct auth_hdr *ahp)
{
	u_int8_t orighash[16];
	u_int8_t hash[16];
	struct crypto_tfm *md5 = rc->rc_ec->ec_md5;

	crypto_digest_init(md5);

	memcpy(orighash, ahp->ah_auth, sizeof(ahp->ah_auth));
	memcpy(ahp->ah_auth, ern->ern_reqauth, sizeof(ern->ern_reqauth));

	digest_update(md5, ahp, ntohs(ahp->ah_len));
	digest_update(md5, rc->rc_secret, rc->rc_secretlen);
	crypto_digest_final(md5, hash);

	memcpy(ahp->ah_auth, orighash, sizeof(ahp->ah_auth));
	return (memcmp(ahp->ah_auth, hash, sizeof(hash)) == 0);
}

/*
 * Validate the Message-Authenticator hash.
 */
static int
radius_check_msg_auth(struct radiuscom *rc, struct eapol_auth_radius_node *ern,
	struct auth_hdr *ahp)
{
	u_int8_t ahash[16], hash[16];
	u_int8_t *ap;
	int authlen;

	ap = radius_find_attr(ahp, RAD_ATTR_MESSAGE_AUTHENTICATOR);
	if (ap == NULL) {
		eapolstats.rs_nomsgauth++;
		return FALSE;
	}
	authlen = ap[1] - 2;
	if (authlen != sizeof(ahp->ah_auth)) {
		eapolstats.rs_lenmismatch++;
		return FALSE;
	}
	memcpy(ahash, ap+2, authlen);	/* save hash */
	memset(ap+2, 0, authlen);	/* zero it for calculations */
	/* NB: we smash ah_auth and don't repair it... */
	memcpy(ahp->ah_auth, ern->ern_reqauth, authlen);

	eapol_hmac_md5(rc->rc_ec, ahp, ntohs(ahp->ah_len),
		rc->rc_secret, rc->rc_secretlen, hash);
	if (memcmp(ahash, hash, sizeof(hash)) == 0) {
		memcpy(ap+2, ahash, authlen);	/* restore original data */
		return TRUE;			/* pass */
	} else {
		return FALSE;			/* fail */
	}
}

/*
 * Create an EAPOL/EAP reply message for the supplicant
 * based on a message from the radius server.  We reserve
 * space at the front for the EAPOL header and then copy
 * all the EAP message items from the radius packet.
 */
static struct sk_buff *
radius_create_reply(struct radiuscom *rc,
	struct auth_hdr *ahp, struct eapol_auth_node *ean)
{
	int len = ntohs(ahp->ah_len);
	const u_int8_t *ap = (const u_int8_t *)&ahp[1];
	const u_int8_t *ep = ap + (len - sizeof(*ahp));
	struct sk_buff *skb;

	/*
	 * Use the received length to size the payload
	 * in the reply.  This is likely an overestimate but
	 * simplifies things. Note also that eapol_alloc_skb
	 * reserves headroom for the headers, including the
	 * EAPOL header.
	 */
	skb = eapol_alloc_skb(len);
	if (skb == NULL) {
		IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_ANY,
		    ("[%s] could not alloc sk_buff (%s)\n",
		    ether_sprintf(ean->ean_node->ni_macaddr), __func__));
		return NULL;
	}
	/*
	 * Copy EAP messages from radius message to the sk_buff.
	 */
	for (; ap < ep; ap += ap[1]) {
		/* validate attribute length */
		if (ap[1] < 2 || ap + ap[1] > ep) {
			IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_RADIUS,
			    ("[%s] bad attribute length %u (%s)\n",
			    ether_sprintf(ean->ean_node->ni_macaddr),
			    ap[1], __func__));
			eapolstats.rs_badattrlen++;
			goto bad;
		}
		if (ap[0] == RAD_ATTR_EAP_MESSAGE) {
			int alen = ap[1] - 2;
			memcpy(skb_put(skb, alen), ap+2, alen);
		}
	}
	/*
	 * Check result.
	 */
	if (skb->len < sizeof(struct eap_hdr_short)) {
		/* no EAP messages were found, discard the buf */
		IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_RADIUS,
		    ("[%s] no EAP msg found (%s)\n",
		    ether_sprintf(ean->ean_node->ni_macaddr), __func__));
		eapolstats.rs_emptyreply++;
bad:
		dev_kfree_skb(skb);
		return NULL;
	} else {
		/* extract EAP identifier for later use */
		struct eap_hdr_short *eap = (struct eap_hdr_short *)skb->data;
		ean->ean_idFromServer = eap->eap_id;
		return skb;
	}
}

#define	RAD_VENDOR_MICROSOFT	311
#define	RAD_KEYTYPE_SEND	16	/* MPPE-Send-Key */
#define	RAD_KEYTYPE_RECV	17	/* MPPE-Recv-Key */

/*
 * Microsoft Vendor-specific radius attributes used for
 * distributing keys.  See MS-MPPE-Send-Key and
 * MS-MPPE-Recv-Key in RFC 2548 for documentation on the
 * handling of this data.
 */
struct vendor_key {
	u_int32_t	vk_vid;		/* vendor id */
	u_int8_t	vk_type;	/* key type */
	u_int8_t	vk_len;		/* length of type+len+salt+key */
	u_int16_t	vk_salt;
	/* variable length key data follows */
} __attribute__((__packed__));

/*
 * Extract a station transmit/receive key by mixing the
 * MPPE key with an md5 hash of the shared secret, request
 * authenticator, salt, and MPPE key contents.
 */
static int
radius_extract_mppe(struct radiuscom *rc, struct eapol_auth_radius_node *ern,
	struct vendor_key *vk, struct eapol_radius_key *key)
{
	u_int8_t hash[16], keybuf[sizeof(key->rk_data)];
	u_int8_t *vkey;
	u_int8_t *dkey;
	int keylen, total, i, j;
	struct crypto_tfm *md5 = rc->rc_ec->ec_md5;

	/*
	 * vk_len is the vendor attribute payload size, so deduct
	 * the header (including salt) to get the key length.  If
	 * the key length is not a multiple of the md5 hash size;
	 * then it is zero-padded (the rfc talks about an optional
	 * padding sub-field but we assume clients may not include
	 * it--this may be wrong if they include it and do not use
	 * zero for padding, in practice everyone so far just sends
	 * keys that are a multiple of 16).  Also, verify the result
	 * fits in the storage we have set aside for it.
	 */
	keylen = vk->vk_len - (sizeof(vk->vk_type) +
		sizeof(vk->vk_len) + sizeof(vk->vk_salt));
	/* XXX is keylen == 0 ok? */
	total = roundup(keylen, 16);
	if (total > sizeof(keybuf)) {
		IEEE80211_DPRINTF(ern->ern_ic, IEEE80211_MSG_ANY,
		    ("[%s] MPPE key too large, total %u (%s)\n",
		    ether_sprintf(ern->ern_node->ni_macaddr),
		    total, __func__));
		/* XXX should we just truncate it? */
		eapolstats.rs_vkeytoolong++;
		return FALSE;
	}

	/*
	 * Create intermediate, potentially zero-padded, copy.
	 */
	vkey = (u_int8_t *)&vk[1];
	memcpy(keybuf, vkey, keylen);
	if (keylen != total)		/* zero-pad key */
		memset(keybuf+keylen, 0, total-keylen);

	/*
	 * The first 16 bytes are xor'd with a hash constructed
	 * from (shared secret | req-authenticator | salt).
	 * Subsequent 16-byte chunks of the key are xor'd with
	 * the hash of (shared secret | key).
	 */
	dkey = key->rk_data;
	for (i = 0; i < total; i += 16) {
		crypto_digest_init(md5);
		digest_update(md5, rc->rc_secret, rc->rc_secretlen);
		if (i == 0) {
			digest_update(md5,
				ern->ern_reqauth, sizeof(ern->ern_reqauth));
			digest_update(md5, &vk->vk_salt, sizeof(vk->vk_salt));
		} else {
			digest_update(md5, &keybuf[i-16], 16);
		}
		crypto_digest_final(md5, hash);

		for (j = 0; j < 16; j++)
			dkey[i+j] = keybuf[i+j] ^ hash[j];
	}
	return TRUE;
}

/*
 * Extract Microsoft MPPE key state from the frame.
 */
static int
radius_extract_key(struct radiuscom *rc,
	struct eapol_auth_radius_node *ern, struct auth_hdr *ahp)
{
#define	ALLKEYS	(RAD_KEYTYPE_SEND+RAD_KEYTYPE_RECV)
	int len = ntohs(ahp->ah_len);
	u_int8_t *ap = (u_int8_t *)&ahp[1];
	u_int8_t *ep = ap + (len - sizeof(*ahp));
	struct vendor_key *vk;
	int found = 0;

	for (; ap < ep && found != ALLKEYS; ap += ap[1]) {
		/* validate attribute length */
		if (ap[1] < 2 || ap + ap[1] > ep) {
			eapolstats.rs_badattrlen++;
			break;
		}
		if (ap[0] != RAD_ATTR_VENDOR_SPECIFIC)
			continue;
		if (ap[1] < 2+sizeof(struct vendor_key)) {
			eapolstats.rs_vkeytooshort++;
			continue;
		}
		vk = (struct vendor_key *)&ap[2];
		/* XXX ntohl alignment */
		if (ntohl(vk->vk_vid) != RAD_VENDOR_MICROSOFT) {
			eapolstats.rs_vkeybadvid++;
			continue;
		}
		if ((ntohs(vk->vk_salt) & 0x8000) == 0) {
			eapolstats.rs_vkeybadsalt++;
			continue;
		}
		switch (vk->vk_type) {
		case RAD_KEYTYPE_SEND:
			if (found && vk->vk_salt == ern->ern_rxkey.rk_salt) {
				eapolstats.rs_vkeydupsalt++;
				continue;
			}
			if (radius_extract_mppe(rc, ern, vk, &ern->ern_txkey)) {
#ifdef IEEE80211_DEBUG
				if (ieee80211_msg_dumpradkeys(ern->ern_ic))
					radius_dumpbytes("send key",
						&ern->ern_txkey.rk_data[1],
						ern->ern_txkey.rk_len);
#endif
				ern->ern_txkey.rk_salt = vk->vk_salt;
				found += RAD_KEYTYPE_SEND;
			}
			break;
		case RAD_KEYTYPE_RECV:
			if (found && vk->vk_salt == ern->ern_txkey.rk_salt) {
				eapolstats.rs_vkeydupsalt++;
				continue;
			}
			if (radius_extract_mppe(rc, ern, vk, &ern->ern_rxkey)) {
#ifdef IEEE80211_DEBUG
				if (ieee80211_msg_dumpradkeys(ern->ern_ic))
					radius_dumpbytes("recv key",
						&ern->ern_rxkey.rk_data[1],
						ern->ern_rxkey.rk_len);
#endif
				ern->ern_rxkey.rk_salt = vk->vk_salt;
				found += RAD_KEYTYPE_RECV;
			}
			break;
		}
	}
	return (found == ALLKEYS);
#undef ALLKEYS
}

/*
 * Locate an attribute in the message and sanity
 * check the length field so the caller can assume
 * it's minimally valid (i.e. it covers the attribute
 * itself and any data is within the bound of the
 * message as specified in the header.
 */
static u_int8_t *
radius_find_attr(struct auth_hdr *ahp, int attr)
{
	int len = ntohs(ahp->ah_len);
	u_int8_t *ap = (u_int8_t *)&ahp[1];
	u_int8_t *ep = ap + (len - sizeof(*ahp));

	for (; ap < ep; ap += ap[1]) {
		/* validate attribute length */
		if (ap[1] < 2 || ap + ap[1] > ep) {
			eapolstats.rs_badattrlen++;
			return NULL;
		}
		if (ap[0] == attr)
			return ap;
	}
	return NULL;
}

/*
 * Helper functions for constructing a radius message.
 */

static u_int8_t *
radius_add_bytes(u_int8_t *dp, int attr, const void *val, u_int len)
{
	dp[0] = attr;
	dp[1] = 2 + len;
	memcpy(&dp[2], val, len);
	return dp + dp[1];
}

static u_int8_t *
radius_add_space(u_int8_t *dp, int attr, u_int len)
{
	dp[0] = attr;
	dp[1] = 2 + len;
	memset(&dp[2], 0, len);
	return dp + dp[1];
}

static u_int8_t *
radius_add_integer(u_int8_t *dp, int attr, u_int32_t val)
{
	val = htonl(val);
	return radius_add_bytes(dp, attr, &val, sizeof(val));
}

static u_int8_t *
radius_add_string(u_int8_t *dp, int attr, const char *val)
{
	return radius_add_bytes(dp, attr, val, strlen(val));
}

static u_int8_t *
radius_add_stationid(u_int8_t *dp, int attr, struct eapol_auth_node *ean)
{
	struct ieee80211com *ic = ean->ean_ic;
	const u_int8_t *mac = ic->ic_bss->ni_macaddr;
	char buf[255];

	snprintf(buf, sizeof(buf), "%02x-%02x-%02x-%02x-%02x-%02x:%.*s",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
		ic->ic_des_esslen, ic->ic_des_essid);
	return radius_add_string(dp, attr, buf);
}

static u_int8_t *
radius_add_macaddr(u_int8_t *dp, int attr, struct eapol_auth_node *ean)
{
	const u_int8_t *mac = ean->ean_node->ni_macaddr;
	char buf[20];

	snprintf(buf, sizeof(buf), "%02x-%02x-%02x-%02x-%02x-%02x",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return radius_add_string(dp, attr, buf);
}

static u_int8_t *
radius_add_nasport(u_int8_t *dp, int port)
{
	char buf[32];
	int len = snprintf(buf, sizeof(buf), "STA port # %d", port);
	return radius_add_bytes(dp, RAD_ATTR_NAS_PORT_ID, buf, len);
}

#ifdef IEEE80211_DEBUG
/*
 * Return the name for certain attributes.
 */
static const char *
radius_attrname(int attr)
{
	static char buf[8];

	switch (attr) {
	case RAD_ATTR_USER_NAME:	return "User-Name";
	case RAD_ATTR_USER_PASSWD:	return "User-Passwd";
	case RAD_ATTR_NAS_IP_ADDRESS:	return "NAS-Ip-Address";
	case RAD_ATTR_NAS_PORT:		return "NAS-Port";
	case RAD_ATTR_SERVICE_TYPE:	return "Service-Type";
	case RAD_ATTR_FRAMED_MTU:	return "Framed-MTU";
	case RAD_ATTR_REPLY_MESSAGE:	return "Reply-Message";
	case RAD_ATTR_STATE:		return "State";
	case RAD_ATTR_CLASS:		return "Class";
	case RAD_ATTR_VENDOR_SPECIFIC:	return "Vendor-Specific";
	case RAD_ATTR_SESSION_TIMEOUT:	return "Session-Timeout";
	case RAD_ATTR_CALLED_STATION_ID:return "Called-Station-ID";
	case RAD_ATTR_CALLING_STATION_ID:return "Calling-Station-ID";
	case RAD_ATTR_NAS_IDENTIFIER:	return "NAS-Identifier";
	case RAD_ATTR_NAS_PORT_TYPE:	return "NAS-Port-Type";
	case RAD_ATTR_CONNECT_INFO:	return "Connect-Info";
	case RAD_ATTR_EAP_MESSAGE:	return "EAP-Message";
	case RAD_ATTR_MESSAGE_AUTHENTICATOR: return "Message-Authenticator";
	case RAD_ATTR_NAS_PORT_ID:	return "NAS-Port-ID";
	}
	snprintf(buf, sizeof(buf), "%u", attr);
	return buf;
};

static void
radius_dumpbytes(const char *tag, const void *data, u_int len)
{
	const u_int8_t *tp;

	printf("%s = 0x", tag);
	for (tp = data; len > 0; tp++, len--)
		printf("%02x", *tp);
	printf("\n");
}

/*
 * Dump the contents of a RADIUS msg to the console.
 */
static void
radius_dumppkt(const char *tag, const struct eapol_auth_node *ean,
	const struct auth_hdr *ahp)
{
	int len = ntohs(ahp->ah_len);
	const u_int8_t *ap = (const u_int8_t *)&ahp[1];
	const u_int8_t *ep = ap + (len - sizeof(*ahp));
	const char *attrname;
	u_int32_t v;

	printf("[%s] %s RADIUS msg: code %u id %u len %u\n",
		ether_sprintf(ean->ean_node->ni_macaddr),
		tag, ahp->ah_code, ahp->ah_id, len);
	for (; ap < ep; ap += ap[1]) {
		if (ap + ap[1] > ep) {
			printf("%s: bogus attribute length %u for attr %u\n",
				__func__, ap[1], ap[0]);
			return;
		}
		attrname = radius_attrname(ap[0]);
		len = ap[1] - 2;
		switch (ap[0]) {
		case RAD_ATTR_SERVICE_TYPE:
		case RAD_ATTR_FRAMED_MTU:
		case RAD_ATTR_NAS_PORT_TYPE:
		case RAD_ATTR_NAS_PORT:
			memcpy(&v, ap+2, sizeof(v));
			v = ntohl(v);
			printf("\t%s = %u (0x%x)\n", attrname, v, v);
			break;
		case RAD_ATTR_USER_NAME:
		case RAD_ATTR_CALLED_STATION_ID:
		case RAD_ATTR_CALLING_STATION_ID:
		case RAD_ATTR_NAS_IDENTIFIER:
		case RAD_ATTR_CONNECT_INFO:
		case RAD_ATTR_NAS_PORT_ID:
			printf("\t%s = \"%.*s\"\n", attrname, len, ap+2);
			break;
		case RAD_ATTR_NAS_IP_ADDRESS:
			memcpy(&v, ap+2, sizeof(v));
			v = ntohl(v);
			printf("\t%s = %u.%u.%u.%u\n", attrname,
				(v>>24)&0xff, (v>>16)&0xff,
				(v>>8)&0xff, (v>>0)&0xff);
			break;
		default:
			printf("\t");
			radius_dumpbytes(attrname, ap+2, len);
			break;
		}
	}
}
#endif /* IEEE80211_DEBUG */

/*
 * Create a response msg for transmission to the radius server.
 * The response is an EAP-encapsulated version of the supplicant
 * request that is left in an outbound packet buffer.
 *
 * XXX bounds checking
 */
int
radius_make_response(struct radiuscom *rc, struct eapol_auth_radius_node *ern)
{
	struct eapol_auth_node *ean = &ern->ern_base;
	struct sk_buff *skb = ean->ean_skb;
	struct eap_hdr *eap;
	u_int8_t *dp, *cp, *map;
	u_int8_t msgauth[16];
	struct auth_hdr *ahp;
	u_int len, cc;

	if (skb == NULL) {
		/* XXX something is wrong */
		return FALSE;
	}
	eap = (struct eap_hdr *)skb->data;

	if (ern->ern_radmsg == NULL) {
		/* XXX 4096 */
		MALLOC(ern->ern_radmsg, u_int8_t *, 4096, M_DEVBUF, M_NOWAIT);
		if (ern->ern_radmsg == NULL) {
			eapolstats.rs_nomem++;	/* XXX unique stat */
			return FALSE;
		}
	}
	ahp = (struct auth_hdr *)ern->ern_radmsg;

	ahp->ah_code = RAD_ACCESS_REQUEST;
	ahp->ah_id = eap->eap_id;		/* copy from supplicant */
	/*
	 * Save authenticator hash for use in validating replies
	 * and for extracting MPPE keys returned by the server.
	 */
	get_random_bytes(ern->ern_reqauth, sizeof(ern->ern_reqauth));
	memcpy(ahp->ah_auth, ern->ern_reqauth, sizeof(ern->ern_reqauth));

	/*
	 * Add the attribute-value pairs.
	 */
	dp = (u_int8_t *)&ahp[1];
	map = dp;		/* hold a pointer for fixup later */
	dp = radius_add_space(dp, RAD_ATTR_MESSAGE_AUTHENTICATOR, 16);
	dp = radius_add_integer(dp, RAD_ATTR_SERVICE_TYPE,
		RAD_SERVICE_TYPE_FRAMED);
	dp = radius_add_bytes(dp, RAD_ATTR_USER_NAME,
		ean->ean_id, ean->ean_idlen);
	/* NB: use Ethernet MTU rather than 802.11 MTU */
	dp = radius_add_integer(dp, RAD_ATTR_FRAMED_MTU,
		ETHERMTU - LLC_SNAPFRAMELEN - sizeof(struct eapol_hdr));
	if (ern->ern_statelen != 0)
		dp = radius_add_bytes(dp, RAD_ATTR_STATE,
			ern->ern_state, ern->ern_statelen);
	if (ern->ern_classlen != 0)
		dp = radius_add_bytes(dp, RAD_ATTR_CLASS,
			ern->ern_class, ern->ern_classlen);
	dp = radius_add_stationid(dp, RAD_ATTR_CALLED_STATION_ID, ean);
	dp = radius_add_macaddr(dp, RAD_ATTR_CALLING_STATION_ID, ean);
	dp = radius_add_string(dp, RAD_ATTR_NAS_IDENTIFIER,
		system_utsname.nodename);
	dp = radius_add_integer(dp, RAD_ATTR_NAS_PORT_TYPE,
		RAD_NAS_PORT_TYPE_WIRELESS);
	switch (ean->ean_ic->ic_curmode) {
	case IEEE80211_MODE_11A:
	case IEEE80211_MODE_TURBO:
		dp = radius_add_string(dp, RAD_ATTR_CONNECT_INFO,
			"CONNECT 54Mbps 802.11a");
		break;
	case IEEE80211_MODE_11B:
		dp = radius_add_string(dp, RAD_ATTR_CONNECT_INFO,
			"CONNECT 11Mbps 802.11b");
		break;
	case IEEE80211_MODE_11G:
		dp = radius_add_string(dp, RAD_ATTR_CONNECT_INFO,
			"CONNECT 54Mbps 802.11g");
		break;
	default:
		/* XXX bitch */
		eapolstats.rs_badmode++;
		break;
	}

	/*
	 * Copy in the original EAP message.  Note that
	 * we have to byte swap the length field in the
	 * header as it is put in host order on receipt.
	 */
	len = eap->eap_len;
	eap->eap_len = htons(len);
	for (cp = (u_int8_t *)eap; len > 0; cp += cc, len -= cc) {
		cc = len;
		if (cc > RAD_MAX_ATTR_LEN)
			cc = RAD_MAX_ATTR_LEN;
		dp = radius_add_bytes(dp, RAD_ATTR_EAP_MESSAGE, cp, cc);
	}
	eap->eap_len = ntohs(eap->eap_len);

	dp = radius_add_bytes(dp, RAD_ATTR_NAS_IP_ADDRESS,
		&rc->rc_local.sin_addr.s_addr, sizeof(struct in_addr));
	dp = radius_add_integer(dp, RAD_ATTR_NAS_PORT,
		IEEE80211_NODE_AID(ean->ean_node));
	dp = radius_add_nasport(dp, IEEE80211_NODE_AID(ean->ean_node));

	ern->ern_radmsglen = dp - ern->ern_radmsg;

	/*
	 * Now patch up stuff that required the message be
	 * formulated and calculate the message authenticator
	 * hash.
	 */
	ahp->ah_len = htons(ern->ern_radmsglen);
	eapol_hmac_md5(rc->rc_ec, ern->ern_radmsg, ern->ern_radmsglen,
		rc->rc_secret, rc->rc_secretlen, msgauth);
	memcpy(map+2, msgauth, sizeof(msgauth));

	return TRUE;
}

/*
 * Callback from the 802.1x state machine on
 * receiving an Identity packet from the supplicant.
 */
static void
radius_identity_input(struct eapol_auth_node *ean, struct sk_buff *skb)
{
	struct eapol_auth_radius_node *ern = EAPOL_RADIUSNODE(ean);

	memset(ern->ern_state, 0, sizeof(ern->ern_state));
	ern->ern_statelen = 0;
	memset(ern->ern_class, 0, sizeof(ern->ern_class));
	ern->ern_classlen = 0;
}

/*
 * Callback from the 802.1x backend state machine to
 * send/resend a response message to the server.
 */
static void
sendRespToServer(struct eapol_auth_node *ean)
{
	struct eapol_auth_radius_node *ern = EAPOL_RADIUSNODE(ean);
	struct radiuscom *rc = ean->ean_ec->ec_radius;
	struct msghdr msg;
	struct iovec iov;

	if (ean->ean_reqSrvCount == 0) {
		/*
		 * Not a retry; formulate the message from scratch
		 * and add an entry to the reply list.  This entry
		 * stays until a response is received from the server
		 * or the node is reset/reclaimed.
		 */
		radius_make_response(rc, ern);
		radius_add_reply(rc, ern);
	}
	IEEE80211_DPRINTF(ean->ean_ic,
		IEEE80211_MSG_RADIUS | IEEE80211_MSG_DOT1X,
		("[%s] %s buf 0x%p len %u\n",
		ether_sprintf(ean->ean_node->ni_macaddr),
		__func__, ern->ern_radmsg, ern->ern_radmsglen));
#ifdef IEEE80211_DEBUG
	if (ieee80211_msg_dumpradius(ean->ean_ic))
		radius_dumppkt("SEND", ean, (struct auth_hdr *)ern->ern_radmsg);
#endif
	msg.msg_name = &rc->rc_server;
	msg.msg_namelen = sizeof(rc->rc_server);
	iov.iov_base = ern->ern_radmsg;
	iov.iov_len = ern->ern_radmsglen;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;
	/* XXXX check return */
	(void) sock_sendmsg(rc->rc_sock, &msg, iov.iov_len);
}

/*
 * Callback from the 802.1x backend state machine to
 * transmit/retransmit the EAP message from the radius sever
 * to the supplicant.  The message should have been setup by
 * the thread that receives messages from the radius server
 * (see radius_create_reply).
 */
static void
txReq(struct eapol_auth_node *ean)
{
	struct eapol_auth_radius_node *ern = EAPOL_RADIUSNODE(ean);
	struct sk_buff *skb;

	if (ern->ern_msg == NULL) {
		IEEE80211_DPRINTF(ean->ean_ic,
			IEEE80211_MSG_RADIUS | IEEE80211_MSG_DOT1X,
			("[%s] no msg to send (%s)!\n",
			ether_sprintf(ean->ean_node->ni_macaddr), __func__));
		eapolstats.rs_nomsg++;
		return;
	}
	skb = skb_clone(ern->ern_msg, GFP_ATOMIC);
	if (skb == NULL) {
		IEEE80211_DPRINTF(ean->ean_ic,
			IEEE80211_MSG_RADIUS | IEEE80211_MSG_DOT1X,
			("[%s] could not clone msg for xmit (%s)\n",
			ether_sprintf(ean->ean_node->ni_macaddr), __func__));
		eapolstats.rs_noclone++;
	} else {
		IEEE80211_DPRINTF(ean->ean_ic,
			IEEE80211_MSG_RADIUS | IEEE80211_MSG_DOT1X,
			("[%s] %s msg 0x%p len %u\n",
			ether_sprintf(ean->ean_node->ni_macaddr),
			__func__, skb, skb->len));
		eapol_send(&ean->ean_base, skb, EAPOL_TYPE_EAP);
	}
}

/*
 * Construct and send an EAPOL RC4 Key message to the supplicant.
 */
static void
radius_txkey(struct eapol_auth_radius_node *ern,
	int keytype, int keyix, struct ieee80211_key *key)
{
	struct eapol_auth_node *ean = &ern->ern_base;
	struct radiuscom *rc = ean->ean_ec->ec_radius;
	struct sk_buff *skb;
	struct eapol_key *kp;
	struct eapol_hdr *eapol;
	struct rc4_state ctx;
	u_int8_t *keydata, rc4key[16+64], hash[16];
	struct crypto_tfm *md5 = rc->rc_ec->ec_md5;

	KASSERT(keyix != IEEE80211_KEYIX_NONE, ("no key index"));
	if (key->wk_cipher->ic_cipher != IEEE80211_CIPHER_WEP) {
		IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_ANY,
		    ("[%s] key type %u not WEP\n",
		    ether_sprintf(ean->ean_node->ni_macaddr),
		    key->wk_cipher->ic_cipher));
		return;
	}
	if (key->wk_keylen != 16 && key->wk_keylen != 13 && key->wk_keylen != 5) {
		IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_ANY,
		    ("[%s] bad key length %u\n",
		    ether_sprintf(ean->ean_node->ni_macaddr), key->wk_keylen));
		return;
	}
	skb = eapol_alloc_skb(sizeof(struct eapol_key) + key->wk_keylen);
	if (skb == NULL) {
		IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_ANY,
		    ("[%s] could not alloc sk_buff (%s)\n",
		    ether_sprintf(ean->ean_node->ni_macaddr), __func__));
		return;
	}
	kp = (struct eapol_key *)skb_put(skb, sizeof(struct eapol_key));
	memset(kp, 0, sizeof(struct eapol_key));
	kp->ek_type = EAPOL_KEY_TYPE_RC4;
	kp->ek_length = htons(key->wk_keylen);
	/* NB: there is no htonll */
	kp->ek_replay = cpu_to_be64(get_jiffies_64());

	get_random_bytes(kp->ek_iv, sizeof(kp->ek_iv));
	crypto_digest_init(md5);
	digest_update(md5, kp->ek_iv, sizeof(kp->ek_iv));
	crypto_digest_final(md5, kp->ek_iv);

	kp->ek_index = keyix | keytype;

	/*
	 * NB: this ``cannot happen'' as the rxkey has already been
	 *     verified to fit in ern_rxkey.
	 */
	if (sizeof(kp->ek_iv)+ern->ern_rxkey.rk_len > sizeof(rc4key)) {
		IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_ANY,
		    ("[%s] intermediate key buf too small, key len %u (%s)\n",
		    ether_sprintf(ean->ean_node->ni_macaddr),
		    ern->ern_rxkey.rk_len, __func__));
		return;
	}

	/* encrypt the station key with rc4 key of (iv | rxkey) */
	memcpy(rc4key, kp->ek_iv, sizeof(kp->ek_iv));
	memcpy(rc4key+sizeof(kp->ek_iv), &ern->ern_rxkey.rk_data[1],
		ern->ern_rxkey.rk_len);

	keydata = (u_int8_t *)skb_put(skb, key->wk_keylen);
	arc4_setkey(&ctx, rc4key, sizeof(kp->ek_iv) + ern->ern_rxkey.rk_len);
	arc4_encrypt(&ctx, keydata, key->wk_key, key->wk_keylen);
	memset(rc4key, 0, sizeof(rc4key));

	/*
	 * Finally, sign the message.  To do this we must
	 * formulate the EAPOL header so we can calculate
	 * the signature that goes in the key payload.  This
	 * violates the normal layering (sigh, who designs
	 * these protocols).
	 */
	eapol = (struct eapol_hdr *)skb_push(skb, sizeof(struct eapol_hdr));
	eapol->eapol_ver = EAPOL_VERSION;
	eapol->eapol_type = EAPOL_TYPE_KEY;
	eapol->eapol_len = htons(skb->len - sizeof(struct eapol_hdr));

	eapol_hmac_md5(rc->rc_ec, eapol, skb->len,
		&ern->ern_txkey.rk_data[1], ern->ern_txkey.rk_len, hash);
	memcpy(kp->ek_sig, hash, sizeof(hash));

	IEEE80211_DPRINTF(ean->ean_ic, IEEE80211_MSG_RADIUS,
	    ("[%s] SEND %s EAPOL-Key ix %u len %u bits\n",
	    ether_sprintf(ean->ean_node->ni_macaddr),
	    keytype == EAPOL_KEY_BCAST ? "bcast" : "ucast",
	    keyix, key->wk_keylen * NBBY));

	eapol_send_raw(&ean->ean_base, skb);
}

/*
 * Construct unicast key for station.
 *
 * XXX just use random data for now; probably want something better
 * XXX cipher suites
 */
static int
setUnicastKey(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct ieee80211_key *key = &ni->ni_ucastkey;
	int ok = 1;

	ieee80211_key_update_begin(ic);
	key->wk_keylen = ni->ni_rsn.rsn_ucastkeylen;
	get_random_bytes(key->wk_key, key->wk_keylen);
	if (!ieee80211_crypto_newkey(ic, ni->ni_rsn.rsn_ucastcipher, key)) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_RADIUS,
		    ("[%s] problem creating unicast key for station\n",
		    ether_sprintf(ni->ni_macaddr)));
		ok = 0;
	} else if (!ieee80211_crypto_setkey(ic, key, ni->ni_macaddr)) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_RADIUS,
		    ("[%s] problem installing unicast key for station\n",
		    ether_sprintf(ni->ni_macaddr)));
		/* XXX recovery? */
		ok = 0;
	}
	ieee80211_key_update_end(ic);

	return ok;
}

/*
 * Callback from the 802.1x backend state machine to transmit
 * broadcast+unicast keys to the supplicant. The keys are
 * constructed from the key material received from the radius
 * server when access is granted.
 *
 * NB: there must be a multicast key; the unicast key is optional.
 */
static void
txKey(struct eapol_auth_node *ean)
{
	struct eapol_auth_radius_node *ern = EAPOL_RADIUSNODE(ean);
	struct ieee80211com *ic = ean->ean_ic;
	struct ieee80211_node *ni = ean->ean_node;

	KASSERT(ean->ean_keyAvailable == TRUE, ("no keys!"));

	/*
	 * Send multicast key with index > 0.
	 */
	if (ic->ic_def_txkey == IEEE80211_KEYIX_NONE) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_RADIUS,
		    ("[%s] no multicast key for station\n",
		    ether_sprintf(ni->ni_macaddr)));
		return;
	}
	radius_txkey(ern, EAPOL_KEY_BCAST,
		ic->ic_def_txkey, &ic->ic_nw_keys[ic->ic_def_txkey]);
	/*
	 * Construct unicast key for station and send it with index 0.
	 */
	if (setUnicastKey(ic, ni))
		radius_txkey(ern, EAPOL_KEY_UCAST, 0, &ni->ni_ucastkey);
}

/*
 * Look for a session waiting for a reply from the
 * radius server.  If found the session is taken off
 * the list.
 */
static struct eapol_auth_radius_node *
radius_get_reply(struct radiuscom *rc, u_int8_t id)
{
	struct eapol_auth_radius_node *ern;

	EAPOL_LOCK_ASSERT(rc->rc_ec);

	LIST_FOREACH(ern, &rc->rc_replies, ern_next)
		if (ern->ern_base.ean_currentId == id) {
			LIST_REMOVE(ern, ern_next);
			ern->ern_onreply = FALSE;
			return ern;
		}
	return NULL;
}

static void
radius_remove_reply(struct radiuscom *rc, struct eapol_auth_radius_node *p)
{
	struct eapol_auth_radius_node *ern;

	EAPOL_LOCK_ASSERT(rc->rc_ec);

	LIST_FOREACH(ern, &rc->rc_replies, ern_next)
		if (ern == p) {
			LIST_REMOVE(ern, ern_next);
			ern->ern_onreply = FALSE;
			break;
		}
}

/*
 * Add a session to the list of those waiting for a
 * reply from the radius server.
 */
static void
radius_add_reply(struct radiuscom *rc, struct eapol_auth_radius_node *ern)
{

	EAPOL_LOCK_ASSERT(rc->rc_ec);

	if (!ern->ern_onreply) {
		LIST_INSERT_HEAD(&rc->rc_replies, ern, ern_next);
		ern->ern_onreply = TRUE;
	}
}

/*
 * Radius-specific node allocate/free routines that
 * allocate the larger nodes needed when communicating
 * with a radius server.  This routine is installed
 * when we attach to the authenticator
 */
static struct eapol_auth_node *
radius_node_alloc(struct eapolcom *ec)
{
	struct eapol_auth_radius_node *ern;

	MALLOC(ern, struct eapol_auth_radius_node *,
		sizeof(struct eapol_auth_radius_node),
		M_EAPOL_NODE, M_NOWAIT | M_ZERO);
	return ern ? &ern->ern_base : NULL;
}

/*
 * Reclaim resources specific to the radius server.  This
 * routine is installed when we attach to the authenticator.
 */
static void
radius_node_free(struct eapol_auth_node *ean)
{
	struct eapol_auth_radius_node *ern = EAPOL_RADIUSNODE(ean);
	struct radiuscom *rc = ean->ean_ec->ec_radius;

	/*
	 * NB: we're called from eapol_node_leave which drops it's
	 * EAPOL lock before calling us as it's already removed the
	 * reference to us from the table and needs to drop the lock
	 * before calling back in to the 802.11 layer.  However we
	 * need to grab the lock again to safeguard removing any
	 * reference to use on the reply list.  There's probably
	 * a better way to do this.
	 */
	EAPOL_LOCK(ean->ean_ec);
	radius_remove_reply(rc, ern);
	EAPOL_UNLOCK(ean->ean_ec);

	if (ern->ern_msg != NULL)
		kfree_skb(ern->ern_msg);
	if (ern->ern_radmsg != NULL)
		FREE(ern->ern_radmsg, M_DEVBUF);
	(*rc->rc_node_free)(ean);
}

/*
 * Reset non-fsm state for the specified node.  This
 * routine is installed when we attach to the authenticator.
 */
static void
radius_node_reset(struct eapol_auth_node *ean)
{
	struct eapol_auth_radius_node *ern = EAPOL_RADIUSNODE(ean);
	struct radiuscom *rc = ean->ean_ec->ec_radius;

	EAPOL_LOCK_ASSERT(rc->rc_ec);

	radius_remove_reply(rc, ern);
	if (ern->ern_msg != NULL) {
		kfree_skb(ern->ern_msg);
		ern->ern_msg = NULL;
	}
	(*rc->rc_node_reset)(ean);
}

/*
 * Cleanup radius client state.
 */
static void
radius_cleanup(struct radiuscom *rc)
{
	if (rc->rc_sock != NULL)
		sock_release(rc->rc_sock);
	if (rc->rc_secret != NULL)
		FREE(rc->rc_secret, M_DEVBUF);
	FREE(rc, M_DEVBUF);

	printk(KERN_INFO "802.1x radius client stopped\n");
}

/*
 * Detach a radius client from an authenticator.
 */
static void
ieee80211_radius_detach(struct eapolcom *ec)
{
	struct radiuscom *rc = ec->ec_radius;

	if (rc != NULL) {
		printk(KERN_INFO "802.1x radius client stopping\n");
		ec->ec_radius = NULL;
		/* restore original methods */
		rc->rc_ec->ec_node_alloc = rc->rc_node_alloc;
		rc->rc_ec->ec_node_free = rc->rc_node_free;
		rc->rc_ec->ec_node_reset = rc->rc_node_reset;
		/*
		 * If we started the receiver thread then just signal
		 * it and let it complete the cleanup work when it's
		 * dropped out of the receive loop.  Otherwise we must
		 * do the cleanup directly.
		 *
		 * XXX wrong, must wait to remove module reference
		 */
		if (rc->rc_pid != -1)
			kill_proc(rc->rc_pid, SIGKILL, 1);
		else
			radius_cleanup(rc);

		_MOD_DEC_USE(THIS_MODULE);
	}
}

/*
 * Attach a radius client to an authenticator.  We setup
 * private state, override the node allocation alloc/free
 * methods, and start a thread to receive messages from
 * the radius server.  We also setup callsbacks used by
 * the 802.1x backend state machine.
 */
static int
ieee80211_radius_attach(struct eapolcom *ec)
{
	struct radiuscom *rc;
	int error, secretlen;

	_MOD_INC_USE(THIS_MODULE, return FALSE);

	/* XXX require master key be setup? */
	secretlen = strlen(radius_secret);
	if (secretlen == 0) {
		printf("%s: no radius server shared secret setup", __func__);
		eapolstats.rs_nosecret++;
		_MOD_DEC_USE(THIS_MODULE);
		return FALSE;
	}
	MALLOC(rc, struct radiuscom *, sizeof(struct radiuscom),
		M_DEVBUF, M_NOWAIT | M_ZERO);
	if (rc == NULL) {
		printf("%s: unable to allocate memory for client state\n",
			__func__);
		eapolstats.rs_nomem++;
		_MOD_DEC_USE(THIS_MODULE);
		return FALSE;
	}
	LIST_INIT(&rc->rc_replies);
	rc->rc_pid = -1;
	MALLOC(rc->rc_secret, u_int8_t*, radius_secretlen, M_DEVBUF, M_NOWAIT);
	if (rc->rc_secret == NULL) {
		eapolstats.rs_nomem++;
		printf("%s: unable to allocate memory for shared secret\n",
			__func__);
		goto bad;
	}
	memcpy(rc->rc_secret, radius_secret, radius_secretlen);
	rc->rc_secretlen = radius_secretlen;

	rc->rc_local = radius_clientaddr;
	error = sock_create(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &rc->rc_sock);
	if (error < 0) {
		printf("%s: cannot create socket, error %u\n",
			__func__, -error);
		eapolstats.rs_nosocket++;
		goto bad;
	}
	/*
	 * Force atomic allocation since we sometimes send
	 * messages from interrupt level.
	 */
/* XXX 2.6.0 is just a guess */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	rc->rc_sock->sk->allocation = GFP_ATOMIC;
#else
	rc->rc_sock->sk->sk_allocation = GFP_ATOMIC;
#endif
	error = (*rc->rc_sock->ops->bind)(rc->rc_sock,
	    (struct sockaddr *)&rc->rc_local, sizeof(rc->rc_local));
	if (error < 0) {
		/* XXX */
		printf("%s: cannot bind local address, error %u\n",
			__func__, -error);
		eapolstats.rs_cannotbind++;
		goto bad;
	}
	/* XXX check result */

	rc->rc_server = radius_serveraddr;
	rc->rc_pid = kernel_thread(radiusd, rc, CLONE_KERNEL);
	if (rc->rc_pid < 0) {
		printf("%s: cannot start radiusd thread; error %d\n",
			__func__, -rc->rc_pid);
		eapolstats.rs_nothread++;
		goto bad;
	}
	rc->rc_identity_input = radius_identity_input;
	rc->rc_txreq = txReq;
	rc->rc_sendsrvr = sendRespToServer;
	rc->rc_txkey = txKey;
	/*
	 * Override node management methods so we have the
	 * extra space needed for the radius client and so
	 * we properly manage our private state.
	 */
	rc->rc_node_alloc = ec->ec_node_alloc;
	ec->ec_node_alloc = radius_node_alloc;
	rc->rc_node_free = ec->ec_node_free;
	ec->ec_node_free = radius_node_free;
	rc->rc_node_reset = ec->ec_node_reset;
	ec->ec_node_reset = radius_node_reset;

	rc->rc_ec = ec;
	ec->ec_radius = rc;

	printk(KERN_INFO "802.1x radius client started\n");
	return TRUE;
bad:
	ieee80211_radius_detach(ec);		/* NB: does _MOD_DEC_USE */
	return FALSE;
}

/*
 * Handle a sysctl that read/writes an IP address.
 */
static int
radius_sysctl_ipaddr(ctl_table *ctl, int write, struct file *filp,
	void *buf, size_t *lenp)
{
	struct sockaddr_in *sin = (struct sockaddr_in *) ctl->data;

	if (write) {
		u_int a1, a2, a3, a4;
		if (sscanf(buf, "%d.%d.%d.%d", &a1, &a2, &a3, &a4) != 4)
			return -EINVAL;
		sin->sin_addr.s_addr = htonl(
			  ((a1 & 0xff) << 24) | ((a2 & 0xff) << 16)
			| ((a3 & 0xff) <<  8) | ((a4 & 0xff) <<  0));
	} else if (*lenp && filp->f_pos == 0) {
		u_int32_t v = ntohl(sin->sin_addr.s_addr);
		*lenp = snprintf(buf, *lenp, "%d.%d.%d.%d\n",
			(v >> 24) & 0xff, (v >> 16) & 0xff,
			(v >>  8) & 0xff, (v >>  0) & 0xff);
		filp->f_pos += *lenp;
	} else {
		*lenp = 0;
	}
	return 0;
}

/*
 * Handle a sysctl that reads/writes an IP port number.
 */
static int
radius_sysctl_ipport(ctl_table *ctl, int write, struct file *filp,
	void *buf, size_t *lenp)
{
	struct sockaddr_in *sin = (struct sockaddr_in *) ctl->data;

	if (write) {
		u_int a1;
		if (sscanf(buf, "%d", &a1) != 1)
			return -EINVAL;
		sin->sin_port = htons(a1 & 0xffff);
	} else if (*lenp && filp->f_pos == 0) {
		*lenp = snprintf(buf, *lenp, "%d\n", ntohs(sin->sin_port));
		filp->f_pos += *lenp;
	} else {
		*lenp = 0;
	}
	return 0;
}

/*
 * Handle a sysctl to read/write the radius server shared secret.
 */
static int
radius_sysctl_secret(ctl_table *ctl, int write, struct file *filp,
	void *buf, size_t *lenp)
{
	int len = *lenp;

	if (write) {
		if (len > RAD_MAX_SECRET)	/* should not happen */
			len = RAD_MAX_SECRET;
		memset(radius_secret, 0, sizeof(radius_secret));
		memcpy(radius_secret, buf, len);
		radius_secret[RAD_MAX_SECRET] = '\0';
		radius_secretlen = len;
	} else if (*lenp && filp->f_pos == 0) {
		if (len >= radius_secretlen)
			len = strlen(radius_secret);
		memcpy(buf, radius_secret, len);
		*lenp = len;
		filp->f_pos += *lenp;
	} else {
		*lenp = 0;
	}
	return 0;
}

#define	CTL_AUTO	-2	/* cannot be CTL_ANY or CTL_NONE */

static ctl_table radius_sysctls[] = {
	{ .ctl_name	= CTL_AUTO,
	  .procname	= "serveraddr",
	  .data		= &radius_serveraddr,
	  .mode		= 0644,
	  .proc_handler	= radius_sysctl_ipaddr
	},
	{ .ctl_name	= CTL_AUTO,
	  .procname	= "serverport",
	  .data		= &radius_serveraddr,
	  .mode		= 0644,
	  .proc_handler	= radius_sysctl_ipport
	},
	{ .ctl_name	= CTL_AUTO,
	  .procname	= "clientaddr",
	  .data		= &radius_clientaddr,
	  .mode		= 0644,
	  .proc_handler	= radius_sysctl_ipaddr
	},
	{ .ctl_name	= CTL_AUTO,
	  .procname	= "secret",
	  .maxlen	= RAD_MAX_SECRET,
	  .mode		= 0600,
	  .proc_handler	= radius_sysctl_secret
	},
	{ 0 }
};
static ctl_table radius_table[] = {
	{ .ctl_name	= CTL_AUTO,
	  .procname	= "radius",
	  .mode		= 0555,
	  .child	= radius_sysctls
	}, { 0 }
};
static ctl_table dot1x_table[] = {
	{ .ctl_name	= NET_8021X,
	  .procname	= "8021x",
	  .mode		= 0555,
	  .child	= radius_table
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

static struct ctl_table_header *radius_sys;

/*
 * Module glue.
 */

MODULE_AUTHOR("Errno Consulting, Sam Leffler");
MODULE_DESCRIPTION("802.11 wireless support: radius backend ");
#ifdef MODULE_LICENSE
MODULE_LICENSE("Dual BSD/GPL");
#endif

static const struct ieee80211_authenticator_backend radius = {
	.iab_name	= "radius",
	.iab_attach	= ieee80211_radius_attach,
	.iab_detach	= ieee80211_radius_detach,
};

static int __init
init_ieee80211_radius(void)
{
#ifndef __linux__
	radius_clientaddr.sin_len = sizeof(radius_clientaddr);
#endif
	radius_clientaddr.sin_family = AF_INET;
#ifndef __linux__
	radius_serveraddr.sin_len = sizeof(radius_serveraddr);
#endif
	radius_serveraddr.sin_family = AF_INET;
	radius_serveraddr.sin_port = htons(1812);	/* default port */
	radius_sys = register_sysctl_table(net_table, 0);

	ieee80211_authenticator_backend_register(&radius);
	return 0;
}
module_init(init_ieee80211_radius);

static void __exit
exit_ieee80211_radius(void)
{
	if (radius_sys)
		unregister_sysctl_table(radius_sys);

	ieee80211_authenticator_backend_unregister(&radius);
}
module_exit(exit_ieee80211_radius);
