/*-
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
 * $Id: ieee80211_crypto_wep.c,v 1.1.1.1 2009/08/19 08:02:21 jack Exp $
 */

/*
 * IEEE 802.11 WEP crypto support.
 */
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/random.h>
#include <linux/init.h>

#include "if_media.h"

#include <net80211/ieee80211_var.h>

#include "rc4.h"
#define	arc4_ctxlen()			sizeof (struct rc4_state)
#define	arc4_setkey(_c,_k,_l)		rc4_init(_c,_k,_l)
#define	arc4_encrypt(_c,_d,_s,_l)	rc4_crypt_skip(_c,_s,_d,_l,0)
#define	arc4_decrypt(_c,_d,_s,_l)	rc4_crypt_skip(_c,_s,_d,_l,0)

static	void *wep_attach(struct ieee80211com *, struct ieee80211_key *);
static	void wep_detach(struct ieee80211_key *);
static	int wep_setkey(struct ieee80211_key *);
static	int wep_encap(struct ieee80211_key *, struct sk_buff *, u_int8_t keyid);
static	int wep_decap(struct ieee80211_key *, struct sk_buff *);
static	int wep_enmic(struct ieee80211_key *, struct sk_buff *);
static	int wep_demic(struct ieee80211_key *, struct sk_buff *);

static const struct ieee80211_cipher wep = {
	.ic_name	= "WEP",
	.ic_cipher	= IEEE80211_CIPHER_WEP,
	.ic_header	= IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN,
	.ic_trailer	= IEEE80211_WEP_CRCLEN,
	.ic_miclen	= 0,
	.ic_attach	= wep_attach,
	.ic_detach	= wep_detach,
	.ic_setkey	= wep_setkey,
	.ic_encap	= wep_encap,
	.ic_decap	= wep_decap,
	.ic_enmic	= wep_enmic,
	.ic_demic	= wep_demic,
};

static	int wep_encrypt(struct ieee80211_key *, struct sk_buff *, int hdrlen);
static	int wep_decrypt(struct ieee80211_key *, struct sk_buff *, int hdrlen);

struct wep_ctx_hw {			/* for use with h/w support */
	struct ieee80211com *wc_ic;	/* for diagnostics */
	u_int32_t	wc_iv;		/* initial vector for crypto */
};

struct wep_ctx {
	struct ieee80211com *wc_ic;	/* for diagnostics */
	u_int32_t	wc_iv;		/* initial vector for crypto */
	struct rc4_state wc_rc4;	/* rc4 state */
};

static void *
wep_attach(struct ieee80211com *ic, struct ieee80211_key *k)
{
	struct wep_ctx *ctx;

	_MOD_INC_USE(THIS_MODULE, return NULL);

	/* NB: only allocate rc4 state when doing s/w crypto */
	MALLOC(ctx, struct wep_ctx *,
		k->wk_flags & IEEE80211_KEY_SWCRYPT ?
			sizeof(struct wep_ctx) : sizeof(struct wep_ctx_hw),
		M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ctx == NULL) {
		ic->ic_stats.is_crypto_nomem++;
		_MOD_DEC_USE(THIS_MODULE);
		return NULL;
	}

	ctx->wc_ic = ic;
	get_random_bytes(&ctx->wc_iv, sizeof(ctx->wc_iv));
	return ctx;
}

static void
wep_detach(struct ieee80211_key *k)
{
	struct wep_ctx *ctx = k->wk_private;

	FREE(ctx, M_DEVBUF);

	_MOD_DEC_USE(THIS_MODULE);
}

static int
wep_setkey(struct ieee80211_key *k)
{
	return k->wk_keylen >= 40/NBBY;
}

#ifndef _BYTE_ORDER
#error "Don't know native byte order"
#endif

/*
 * Add privacy headers appropriate for the specified key.
 */
static int
wep_encap(struct ieee80211_key *k, struct sk_buff *skb, u_int8_t keyid)
{
	struct wep_ctx *ctx = k->wk_private;
	u_int32_t iv;
	u_int8_t *ivp;
	int hdrlen;

	hdrlen = ieee80211_hdrsize(skb->data);

	/*
	 * Copy down 802.11 header and add the IV + KeyID.
	 */
	ivp = skb_push(skb, wep.ic_header);
	memmove(ivp, ivp + wep.ic_header, hdrlen);
	ivp += hdrlen;

	/*
	 * XXX
	 * IV must not duplicate during the lifetime of the key.
	 * But no mechanism to renew keys is defined in IEEE 802.11
	 * for WEP.  And the IV may be duplicated at other stations
	 * because the session key itself is shared.  So we use a
	 * pseudo random IV for now, though it is not the right way.
	 *
	 * NB: Rather than use a strictly random IV we select a
	 * random one to start and then increment the value for
	 * each frame.  This is an explicit tradeoff between
	 * overhead and security.  Given the basic insecurity of
	 * WEP this seems worthwhile.
	 */

	/*
	 * Skip 'bad' IVs from Fluhrer/Mantin/Shamir:
	 * (B, 255, N) with 3 <= B < 16 and 0 <= N <= 255
	 */
	iv = ctx->wc_iv;
	if ((iv & 0xff00) == 0xff00) {
		int B = (iv & 0xff0000) >> 16;
		if (3 <= B && B < 16)
			iv += 0x0100;
	}
	ctx->wc_iv = iv + 1;

	/*
	 * NB: Preserve byte order of IV for packet
	 *     sniffers; it doesn't matter otherwise.
	 */
#if _BYTE_ORDER == _BIG_ENDIAN
	ivp[0] = iv >> 0;
	ivp[1] = iv >> 8;
	ivp[2] = iv >> 16;
#else
	ivp[2] = iv >> 0;
	ivp[1] = iv >> 8;
	ivp[0] = iv >> 16;
#endif
	ivp[3] = keyid;

	/*
	 * Finally, do software encrypt if neeed.
	 */
	if ((k->wk_flags & IEEE80211_KEY_SWCRYPT) &&
	    !wep_encrypt(k, skb, hdrlen)) {
		return 0;
	}

	return 1;
}

/*
 * Add MIC to the frame as needed.
 */
static int
wep_enmic(struct ieee80211_key *k, struct sk_buff *skb)
{

	return 1;
}

/*
 * Validate and strip privacy headers (and trailer) for a
 * received frame.  If necessary, decrypt the frame using
 * the specified key.
 */
static int
wep_decap(struct ieee80211_key *k, struct sk_buff *skb)
{
	struct wep_ctx *ctx = k->wk_private;
	struct ieee80211_frame *wh;
	int hdrlen;

	wh = (struct ieee80211_frame *)skb->data;
	hdrlen = ieee80211_hdrsize(wh);

	/*
	 * Check if the device handled the decrypt in hardware.
	 * If so we just strip the header; otherwise we need to
	 * handle the decrypt in software.
	 */
	if ((k->wk_flags & IEEE80211_KEY_SWCRYPT) &&
	    !wep_decrypt(k, skb, hdrlen)) {
		IEEE80211_DPRINTF(ctx->wc_ic, IEEE80211_MSG_CRYPTO,
		    ("[%s] WEP ICV mismatch on decrypt\n",
		    ether_sprintf(wh->i_addr2)));
		ctx->wc_ic->ic_stats.is_rx_wepfail++;
		return 0;
	}

	/*
	 * Copy up 802.11 header and strip crypto bits.
	 */
	memmove(skb->data + wep.ic_header, skb->data, hdrlen);
	skb_pull(skb, wep.ic_header);
	skb_trim(skb, skb->len - wep.ic_trailer);

	return 1;
}

/*
 * Verify and strip MIC from the frame.
 */
static int
wep_demic(struct ieee80211_key *k, struct sk_buff *skb)
{
	return 1;
}

/*
 * CRC 32 -- routine from RFC 2083
 */

/* Table of CRCs of all 8-bit messages */
static u_int32_t crc_table[256];

/* Make the table for a fast CRC. */
static void
crc_init(void)
{
	u_int32_t c;
	int n, k;

	for (n = 0; n < 256; n++) {
		c = (u_int32_t)n;
		for (k = 0; k < 8; k++) {
			if (c & 1)
				c = 0xedb88320UL ^ (c >> 1);
			else
				c = c >> 1;
		}
		crc_table[n] = c;
	}
}

/*
 * Update a running CRC with the bytes buf[0..len-1]--the CRC
 * should be initialized to all 1's, and the transmitted value
 * is the 1's complement of the final running CRC
 */

static u_int32_t
crc_update(u_int32_t crc, u_int8_t *buf, int len)
{
	u_int8_t *endbuf;

	for (endbuf = buf + len; buf < endbuf; buf++)
		crc = crc_table[(crc ^ *buf) & 0xff] ^ (crc >> 8);
	return crc;
}

static int
wep_encrypt(struct ieee80211_key *key, struct sk_buff *skb, int hdrlen)
{
	struct wep_ctx *ctx = key->wk_private;
	u_int8_t rc4key[IEEE80211_WEP_IVLEN + IEEE80211_KEYBUF_SIZE];
	u_int8_t crcbuf[IEEE80211_WEP_CRCLEN];
	u_int8_t *icv;
	u_int32_t crc;

	KASSERT(key->wk_flags & IEEE80211_KEY_SWCRYPT, ("No s/w crypto state"));

	ctx->wc_ic->ic_stats.is_crypto_wep++;

	if (skb_tailroom(skb) < wep.ic_trailer) {
		struct ieee80211_frame *wh =
			(struct ieee80211_frame *) skb->data;
		/* NB: should not happen */
		IEEE80211_DPRINTF(ctx->wc_ic, IEEE80211_MSG_CRYPTO,
			("[%s] No room for %s ICV, tailroom %u\n",
			ether_sprintf(wh->i_addr1), wep.ic_name,
			skb_tailroom(skb)));
		/* XXX statistic */
		return 0;
	}
	memcpy(rc4key, skb->data + hdrlen, IEEE80211_WEP_IVLEN);
	memcpy(rc4key + IEEE80211_WEP_IVLEN, key->wk_key, key->wk_keylen);
	arc4_setkey(&ctx->wc_rc4, rc4key, IEEE80211_WEP_IVLEN + key->wk_keylen);

	/* calculate CRC over unencrypted data */
	crc = crc_update(~0,
		skb->data + hdrlen + wep.ic_header,
		skb->len - (hdrlen + wep.ic_header));
	/* encrypt data */
	arc4_encrypt(&ctx->wc_rc4,
		skb->data + hdrlen + wep.ic_header,
		skb->data + hdrlen + wep.ic_header,
		skb->len - (hdrlen + wep.ic_header));
	/* tack on ICV */
	*(u_int32_t *)crcbuf = htole32(~crc);
	icv = skb_put(skb, IEEE80211_WEP_CRCLEN);
	arc4_encrypt(&ctx->wc_rc4, icv, crcbuf, IEEE80211_WEP_CRCLEN);

	return 1;
}

static int
wep_decrypt(struct ieee80211_key *key, struct sk_buff *skb, int hdrlen)
{
	struct wep_ctx *ctx = key->wk_private;
	u_int8_t rc4key[IEEE80211_WEP_IVLEN + IEEE80211_KEYBUF_SIZE];
	u_int8_t crcbuf[IEEE80211_WEP_CRCLEN];
	u_int8_t *icv;
	u_int32_t crc;

	KASSERT(key->wk_flags & IEEE80211_KEY_SWCRYPT, ("No s/w crypto state"));

	ctx->wc_ic->ic_stats.is_crypto_wep++;

	memcpy(rc4key, skb->data + hdrlen, IEEE80211_WEP_IVLEN);
	memcpy(rc4key + IEEE80211_WEP_IVLEN, key->wk_key, key->wk_keylen);
	arc4_setkey(&ctx->wc_rc4, rc4key, IEEE80211_WEP_IVLEN + key->wk_keylen);

	/* decrypt data */
	arc4_decrypt(&ctx->wc_rc4,
		skb->data + hdrlen + wep.ic_header,
		skb->data + hdrlen + wep.ic_header,
		skb->len - (hdrlen + wep.ic_header + wep.ic_trailer));
	/* calculate CRC over unencrypted data */
	crc = crc_update(~0,
		skb->data + hdrlen + wep.ic_header,
		skb->len - (hdrlen + wep.ic_header + wep.ic_trailer));
	/* decrypt ICV and compare to CRC */
	icv = skb->data + (skb->len - IEEE80211_WEP_CRCLEN);
	arc4_decrypt(&ctx->wc_rc4, crcbuf, icv, IEEE80211_WEP_CRCLEN);

	return (crc == ~le32toh(*(u_int32_t *)crcbuf));
}

/*
 * Module glue.
 */

MODULE_AUTHOR("Errno Consulting, Sam Leffler");
MODULE_DESCRIPTION("802.11 wireless support: WEP cipher");
#ifdef MODULE_LICENSE
MODULE_LICENSE("Dual BSD/GPL");
#endif

static int __init
init_crypto_wep(void)
{
	ieee80211_crypto_register(&wep);
	crc_init();
	return 0;
}
module_init(init_crypto_wep);

static void __exit
exit_crypto_wep(void)
{
	ieee80211_crypto_unregister(&wep);
}
module_exit(exit_crypto_wep);
