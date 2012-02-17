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
 * $Id: ieee80211_crypto.c,v 1.1.1.1 2009/08/19 08:02:21 jack Exp $
 */

__FBSDID("$FreeBSD: src/sys/net80211/ieee80211_crypto.c,v 1.3 2003/10/17 23:15:30 sam Exp $");
__KERNEL_RCSID(0, "$NetBSD: ieee80211_crypto.c,v 1.4 2003/09/23 16:03:46 dyoung Exp $");

/*
 * IEEE 802.11 generic crypto support.
 */
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/random.h>

#include "if_ethersubr.h"		/* XXX ETHER_HDR_LEN */
#include "if_media.h"

#include <net80211/ieee80211_var.h>

/*
 * Table of registered cipher modules.
 */
static	const struct ieee80211_cipher *ciphers[IEEE80211_CIPHER_MAX];

static	int _ieee80211_crypto_delkey(struct ieee80211com *,
		struct ieee80211_key *);

/*
 * Default "null" key management routines.
 */
static int
null_key_alloc(struct ieee80211com *ic, const struct ieee80211_key *k)
{
	return IEEE80211_KEYIX_NONE;
}
static int
null_key_delete(struct ieee80211com *ic, const struct ieee80211_key *k)
{
	return 1;
}
static 	int
null_key_set(struct ieee80211com *ic, const struct ieee80211_key *k,
	     const u_int8_t mac[IEEE80211_ADDR_LEN])
{
	return 1;
}
static void null_key_update(struct ieee80211com *ic) {}

/*
 * Write-arounds for common operations.
 */
static inline void
cipher_detach(struct ieee80211_key *key)
{
	key->wk_cipher->ic_detach(key);
}

static inline void *
cipher_attach(struct ieee80211com *ic, struct ieee80211_key *key)
{
	return key->wk_cipher->ic_attach(ic, key);
}

/* 
 * Wrappers for driver key management methods.
 */
static inline int
dev_key_alloc(struct ieee80211com *ic,
	const struct ieee80211_key *key)
{
	return ic->ic_crypto.cs_key_alloc(ic, key);
}

static inline int
dev_key_delete(struct ieee80211com *ic,
	const struct ieee80211_key *key)
{
	return ic->ic_crypto.cs_key_delete(ic, key);
}

static inline int
dev_key_set(struct ieee80211com *ic, const struct ieee80211_key *key,
	const u_int8_t mac[IEEE80211_ADDR_LEN])
{
	return ic->ic_crypto.cs_key_set(ic, key, mac);
}

/*
 * Setup crypto support.
 */
void
ieee80211_crypto_attach(struct ieee80211com *ic)
{
	struct ieee80211_crypto_state *cs = &ic->ic_crypto;
	int i;

	/* NB: we assume everything is pre-zero'd */
	cs->cs_def_txkey = IEEE80211_KEYIX_NONE;
	ciphers[IEEE80211_CIPHER_NONE] = &ieee80211_cipher_none;
	for (i = 0; i < IEEE80211_WEP_NKID; i++)
		ieee80211_crypto_resetkey(ic, &cs->cs_nw_keys[i], i);
	/*
	 * Initialize the driver key support routines to noop entries.
	 * This is useful especially for the cipher test modules.
	 */
	cs->cs_key_alloc = null_key_alloc;
	cs->cs_key_set = null_key_set;
	cs->cs_key_delete = null_key_delete;
	cs->cs_key_update_begin = null_key_update;
	cs->cs_key_update_end = null_key_update;
}
EXPORT_SYMBOL(ieee80211_crypto_attach);

/*
 * Teardown crypto support.
 */
void
ieee80211_crypto_detach(struct ieee80211com *ic)
{
	ieee80211_crypto_delglobalkeys(ic);
}
EXPORT_SYMBOL(ieee80211_crypto_detach);

/*
 * Register a crypto cipher module.
 */
void
ieee80211_crypto_register(const struct ieee80211_cipher *cip)
{
	if (cip->ic_cipher >= IEEE80211_CIPHER_MAX) {
		printf("%s: cipher %s has an invalid cipher index %u\n",
			__func__, cip->ic_name, cip->ic_cipher);
		return;
	}
	if (ciphers[cip->ic_cipher] != NULL && ciphers[cip->ic_cipher] != cip) {
		printf("%s: cipher %s registered with a different template\n",
			__func__, cip->ic_name);
		return;
	}
	ciphers[cip->ic_cipher] = cip;
}
EXPORT_SYMBOL(ieee80211_crypto_register);

/*
 * Unregister a crypto cipher module.
 */
void
ieee80211_crypto_unregister(const struct ieee80211_cipher *cip)
{
	if (cip->ic_cipher >= IEEE80211_CIPHER_MAX) {
		printf("%s: cipher %s has an invalid cipher index %u\n",
			__func__, cip->ic_name, cip->ic_cipher);
		return;
	}
	if (ciphers[cip->ic_cipher] != NULL && ciphers[cip->ic_cipher] != cip) {
		printf("%s: cipher %s registered with a different template\n",
			__func__, cip->ic_name);
		return;
	}
	/* NB: don't complain about not being registered */
	/* XXX disallow if references */
	ciphers[cip->ic_cipher] = NULL;
}
EXPORT_SYMBOL(ieee80211_crypto_unregister);

/* XXX well-known names! */
static const char *cipher_modnames[] = {
	"wlan_wep",	/* IEEE80211_CIPHER_WEP */
	"wlan_tkip",	/* IEEE80211_CIPHER_TKIP */
	"wlan_aes_ocb",	/* IEEE80211_CIPHER_AES_OCB */
	"wlan_ccmp",	/* IEEE80211_CIPHER_AES_CCM */
	"wlan_ckip",	/* IEEE80211_CIPHER_CKIP */
};

/*
 * Establish a relationship between the specified key and cipher
 * and, if not a global key, allocate a hardware index from the
 * driver.  Note that we may be called for global keys but they
 * should have a key index already setup so the only work done
 * is to setup the cipher reference.
 *
 * This must be the first call applied to a key; all the other key
 * routines assume wk_cipher is setup.
 *
 * Locking must be handled by the caller using:
 *	ieee80211_key_update_begin(ic);
 *	ieee80211_key_update_end(ic);
 */
int
ieee80211_crypto_newkey(struct ieee80211com *ic,
	int cipher, struct ieee80211_key *key)
{
#define	N(a)	(sizeof(a) / sizeof(a[0]))
	const struct ieee80211_cipher *cip;
	void *keyctx;
	int oflags;

	/*
	 * Validate cipher and set reference to cipher routines.
	 */
	if (cipher >= IEEE80211_CIPHER_MAX) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
			("%s: invalid cipher %u\n", __func__, cipher));
		ic->ic_stats.is_crypto_badcipher++;
		return 0;
	}
	cip = ciphers[cipher];
	if (cip == NULL) {
		/*
		 * Auto-load cipher module if we have a well-known name
		 * for it.  It might be better to use string names rather
		 * than numbers and craft a module name based on the cipher
		 * name; e.g. wlan_cipher_<cipher-name>.
		 */
		if (cipher < N(cipher_modnames)) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
				("%s: unregistered cipher %u, load module %s\n",
				__func__, cipher, cipher_modnames[cipher]));
			request_module(cipher_modnames[cipher]);
			/*
			 * If cipher module loaded it should immediately
			 * call ieee80211_crypto_register which will fill
			 * in the entry in the ciphers array.
			 */
			cip = ciphers[cipher];
		}
		if (cip == NULL) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
				("%s: unable to load cipher %u, module %s\n",
				__func__, cipher,
				cipher < N(cipher_modnames) ?
					cipher_modnames[cipher] : "<unknown>"));
			ic->ic_stats.is_crypto_nocipher++;
			return 0;
		}
	}

	oflags = key->wk_flags;
	/*
	 * If the hardware does not support the cipher then
	 * fallback to a host-based implementation.
	 */
	key->wk_flags &= ~(IEEE80211_KEY_SWCRYPT|IEEE80211_KEY_SWMIC);
	if ((ic->ic_caps & (1<<cipher)) == 0) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
		    ("%s: no h/w support for cipher %s, falling back to s/w\n",
		    __func__, cip->ic_name));
		key->wk_flags |= IEEE80211_KEY_SWCRYPT;
	}
	/*
	 * Hardware TKIP with software MIC is an important
	 * combination; we handle it by flagging each key,
	 * the cipher modules honor it.
	 */
	if (cipher == IEEE80211_CIPHER_TKIP &&
	    (ic->ic_caps & IEEE80211_C_TKIPMIC) == 0) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
		    ("%s: no h/w support for TKIP MIC, falling back to s/w\n",
		    __func__));
		key->wk_flags |= IEEE80211_KEY_SWCRYPT;
		key->wk_flags |= IEEE80211_KEY_SWMIC;
	}

	/*
	 * Handle AES in counter mode.
	 */
        if (cipher == IEEE80211_CIPHER_AES_CCM &&
            (ic->ic_caps & IEEE80211_C_AES_CCM) == 0) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
		    ("%s: no h/w support for AES CCMP, falling back to s/w\n",
		    __func__));
		key->wk_flags |= IEEE80211_KEY_SWCRYPT;
	}

	/*
	 * Bind cipher to key instance.  Note we do this
	 * after checking the device capabilities so the
	 * cipher module can optimize space usage based on
	 * whether or not it needs to do the cipher work.
	 */
	if (key->wk_cipher != cip || key->wk_flags != oflags) {
again:
		keyctx = cip->ic_attach(ic, key);
		if (keyctx == NULL) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
				("%s: unable to attach cipher %s\n",
				__func__, cip->ic_name));
			key->wk_flags = oflags;	/* restore old flags */
			ic->ic_stats.is_crypto_attachfail++;
			return 0;
		}
		cipher_detach(key);
		key->wk_cipher = cip;		/* XXX refcnt? */
		key->wk_private = keyctx;
	}

	/*
	 * Ask the driver for a key index if we don't have one.
	 * Note that entries in the global key table always have
	 * an index; this means it's safe to call this routine
	 * for these entries just to setup the reference to the
	 * cipher template.  Note also that when using software
	 * crypto we also call the driver to give us a key index.
	 */
	if (key->wk_keyix == IEEE80211_KEYIX_NONE) {
		key->wk_keyix = dev_key_alloc(ic, key);
		if (key->wk_keyix == IEEE80211_KEYIX_NONE) {
			/*
			 * Driver has no room; fallback to doing crypto
			 * in the host.  We change the flags and start the
			 * procedure over.  If we get back here then there's
			 * no hope and we bail.  Note that this can leave
			 * the key in a inconsistent state if the caller
			 * continues to use it.
			 */
			if ((key->wk_flags & IEEE80211_KEY_SWCRYPT) == 0) {
				ic->ic_stats.is_crypto_swfallback++;
				IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
				    ("%s: no h/w resources for cipher %s, "
				    "falling back to s/w\n", __func__,
				    cip->ic_name));
				oflags = key->wk_flags;
				key->wk_flags |= IEEE80211_KEY_SWCRYPT;
				if (cipher == IEEE80211_CIPHER_TKIP)
					key->wk_flags |= IEEE80211_KEY_SWMIC;
				goto again;
			}
			ic->ic_stats.is_crypto_keyfail++;
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
			    ("%s: unable to setup cipher %s\n",
			    __func__, cip->ic_name));
			return 0;
		}
	}
	return 1;
#undef N
}
EXPORT_SYMBOL(ieee80211_crypto_newkey);

/*
 * Remove the key (no locking, for internal use).
 */
static int
_ieee80211_crypto_delkey(struct ieee80211com *ic, struct ieee80211_key *key)
{
	u_int16_t keyix;

	KASSERT(key->wk_cipher != NULL, ("No cipher!"));

	keyix = key->wk_keyix;
	if (keyix != IEEE80211_KEYIX_NONE) {
		/*
		 * Remove hardware entry.
		 */
		/* XXX key cache */
		if (!dev_key_delete(ic, key)) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
			    ("%s: driver did not delete key index %u\n",
			    __func__, keyix));
			ic->ic_stats.is_crypto_delkey++;
			/* XXX recovery? */
		}
	}
	cipher_detach(key);
	memset(key, 0, sizeof(*key));
	key->wk_cipher = &ieee80211_cipher_none;
	key->wk_private = cipher_attach(ic, key);
	/* NB: cannot depend on key index to decide this */
	if (&ic->ic_nw_keys[0] <= key &&
	    key < &ic->ic_nw_keys[IEEE80211_WEP_NKID])
		key->wk_keyix = keyix;		/* preserve shared key state */
	else
		key->wk_keyix = IEEE80211_KEYIX_NONE;
	return 1;
}

/*
 * Remove the specified key.
 */
int
ieee80211_crypto_delkey(struct ieee80211com *ic, struct ieee80211_key *key)
{
	int status;

	ieee80211_key_update_begin(ic);
	status = _ieee80211_crypto_delkey(ic, key);
	ieee80211_key_update_end(ic);
	return status;
}
EXPORT_SYMBOL(ieee80211_crypto_delkey);

/*
 * Clear the global key table.
 */
void
ieee80211_crypto_delglobalkeys(struct ieee80211com *ic)
{
	int i;

	ieee80211_key_update_begin(ic);
	for (i = 0; i < IEEE80211_WEP_NKID; i++)
		(void) _ieee80211_crypto_delkey(ic, &ic->ic_nw_keys[i]);
	ieee80211_key_update_end(ic);
}
EXPORT_SYMBOL(ieee80211_crypto_delglobalkeys);

/*
 * Set the contents of the specified key.
 *
 * Locking must be handled by the caller using:
 *	ieee80211_key_update_begin(ic);
 *	ieee80211_key_update_end(ic);
 */
int
ieee80211_crypto_setkey(struct ieee80211com *ic, struct ieee80211_key *key,
		const u_int8_t macaddr[IEEE80211_ADDR_LEN])
{
	const struct ieee80211_cipher *cip = key->wk_cipher;

	KASSERT(cip != NULL, ("No cipher!"));

	/*
	 * Give cipher a chance to validate key contents.
	 * XXX should happen before modifying state.
	 */
	if (!cip->ic_setkey(key)) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
		    ("%s: cipher %s rejected key index %u\n",
		    __func__, cip->ic_name, key->wk_keyix));
		ic->ic_stats.is_crypto_setkey_cipher++;
		return 0;
	}
	if (key->wk_keyix == IEEE80211_KEYIX_NONE) {
		/* XXX nothing allocated, should not happen */
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
		    ("%s: no key index; should not happen!\n", __func__));
		ic->ic_stats.is_crypto_setkey_nokey++;
		return 0;
	}
	return dev_key_set(ic, key, macaddr);
}
EXPORT_SYMBOL(ieee80211_crypto_setkey);

/*
 * Add privacy headers appropriate for the specified key.
 */
struct ieee80211_key *
ieee80211_crypto_encap(struct ieee80211com *ic,
	struct ieee80211_node *ni, struct sk_buff *skb)
{
	struct ieee80211_key *k;
	struct ieee80211_frame *wh;
	const struct ieee80211_cipher *cip;
	u_int8_t keyix;

	/*
	 * Multicast traffic always uses the multicast key.
	 * Otherwise if a unicast key is set we use that and
	 * it is always key index 0.  When no unicast key is
	 * set we fall back to the default transmit key.
	 */
	wh = (struct ieee80211_frame *)skb->data;

	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    ni->ni_ucastkey.wk_cipher == &ieee80211_cipher_none) {
		if (ic->ic_def_txkey == IEEE80211_KEYIX_NONE) {
			IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
				("%s: No default xmit key for frame to %s\n",
				__func__, ether_sprintf(wh->i_addr1)));
			ic->ic_stats.is_tx_nodefkey++;
			return NULL;
		}
		keyix = ic->ic_def_txkey;
		k = &ic->ic_nw_keys[ic->ic_def_txkey];
	} else {
		keyix = 0;
		k = &ni->ni_ucastkey;
	}
	cip = k->wk_cipher;
	if (skb_headroom(skb) < cip->ic_header) {
		/*
		 * Should not happen; ieee80211_skbhdr_adjust should
		 * have allocated enough space for all headers.
		 */
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_CRYPTO,
			("%s: Malformed packet for cipher %s; headroom %u\n",
			__func__, cip->ic_name, skb_headroom(skb)));
		ic->ic_stats.is_tx_noheadroom++;
		return NULL;
	}

	return ((*cip->ic_encap)(k, skb, keyix<<6) ? k : NULL);
}
EXPORT_SYMBOL(ieee80211_crypto_encap);

/*
 * Validate and strip privacy headers (and trailer) for a
 * received frame that has the WEP/Privacy bit set.
 */
struct ieee80211_key *
ieee80211_crypto_decap(struct ieee80211com *ic,
	struct ieee80211_node *ni, struct sk_buff *skb)
{
#define	IEEE80211_WEP_HDRLEN	(IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN)
#define	IEEE80211_WEP_MINLEN \
	(sizeof(struct ieee80211_frame) + ETHER_HDR_LEN + \
	IEEE80211_WEP_HDRLEN + IEEE80211_WEP_CRCLEN)
	struct ieee80211_key *k;
	struct ieee80211_frame *wh;
	u_int8_t *ivp;
	u_int8_t keyid;

	/* NB: this minimum size data frame could be bigger */
	if (skb->len < IEEE80211_WEP_MINLEN) {
		IEEE80211_DPRINTF(ic, IEEE80211_MSG_ANY,
			("%s: WEP data frame too short, len %u\n",
			__func__, skb->len));
		ic->ic_stats.is_rx_tooshort++;	/* XXX need unique stat? */
		return NULL;
	}
	/*
	 * Locate the key. If unicast and there is no unicast
	 * key then we fall back to the key id in the header.
	 * This assumes unicast keys are only configured when
	 * the key id in the header is meaningless (typically 0).
	 */
	wh = (struct ieee80211_frame *) skb->data;
	ivp = skb->data + ieee80211_hdrsize(wh);
	keyid = ivp[IEEE80211_WEP_IVLEN];
	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    ni->ni_ucastkey.wk_cipher == &ieee80211_cipher_none)
		k = &ic->ic_nw_keys[keyid >> 6];
	else
		k = &ni->ni_ucastkey;
	return ((*k->wk_cipher->ic_decap)(k, skb) ? k : NULL);
#undef IEEE80211_WEP_MINLEN
#undef IEEE80211_WEP_HDRLEN
}
EXPORT_SYMBOL(ieee80211_crypto_decap);
