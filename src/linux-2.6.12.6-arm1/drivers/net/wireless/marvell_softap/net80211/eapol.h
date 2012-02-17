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
 * $FreeBSD: src/sys/net80211/ieee80211_var.h,v 1.11 2004/01/15 08:44:27 onoe Exp $
 * $Id: eapol.h,v 1.1.1.1 2009/08/19 08:02:21 jack Exp $
 */
#ifndef _NET80211_EAPOL_H_
#define _NET80211_EAPOL_H_

/*
 * Extendible Authentication Protocol over LAN (EAPOL) aka 802.1x.
 */
struct eapol_hdr {
	u_int8_t	eapol_ver;	/* protocol version */
#define	EAPOL_VERSION	0x01
	u_int8_t	eapol_type;	/* packet type */
	u_int16_t	eapol_len;	/* packet body length in bytes */
	/* variable length body follows */
} __attribute__((__packed__));

enum {
	EAPOL_TYPE_EAP		= 0x00,
	EAPOL_TYPE_START	= 0x01,
	EAPOL_TYPE_LOGOFF	= 0x02,
	EAPOL_TYPE_KEY		= 0x03,
	EAPOL_TYPE_EASFA	= 0x04	/* Encapsulated ASF Alert */
};
#define	EAPOL_TYPE_LAST	EAPOL_TYPE_EASFA

/* Ethernet frame type (belongs elsewhere) */
#define	ETH_P_EAPOL	0x888e		/* EAPOL PAE/802.1x */

/*
 * EAPOL key message.
 */
struct eapol_key {
	u_int8_t	ek_type;	/* key type */
#define	EAPOL_KEY_TYPE_RC4	0x01	/* for WEP */
#define	EAPOL_KEY_TYPE_RSN	0x02	/* for 802.11i */
#define	EAPOL_KEY_TYPE_WPA	0xfe	/* different struct, see below */
	u_int16_t	ek_length;	/* frame length */
	u_int64_t	ek_replay;	/* replay counter */
	u_int8_t	ek_iv[16];	/* initialization vector */
	u_int8_t	ek_index;	/* key index */
#define	EAPOL_KEY_BCAST		0x00	/* broadcast */
#define	EAPOL_KEY_UCAST		0x80	/* unicast */
	u_int8_t	ek_sig[16];	/* signature */
	/* variable length data follows */
} __attribute__((__packed__));

/*
 * EAPOL WPA/RSN key message.
 */
struct eapol_wpa_key {
	u_int8_t	ewk_type;	/* EAPOL_KEY_TYPE_WPA */
	u_int16_t	ewk_info;	/* key info */
#define	EAPOL_WKEY_INFO_TYPE	0x0007
#define	EAPOL_WKEY_INFO_MD5	1	/* hmac-md5 & rc4 */
#define	EAPOL_WKEY_INFO_AES	2	/* hmac-sha1 & aes-keywrap */
#define	EAPOL_WKEY_INFO_PW	0x0008
#define	EAPOL_WKEY_INFO_GROUP	0x0000
#define	EAPOL_WKEY_INFO_INDEX	0x0030	/* key index (WPA only) */
#define	EAPOL_WKEY_INFO_INSTALL	0x0040	/* install or tx/rx */
#define	EAPOL_WKEY_INFO_ACK	0x0080	/* STA/AP handshake bit */
#define	EAPOL_WKEY_INFO_MIC	0x0100	/* msg is MIC'd */
#define	EAPOL_WKEY_INFO_SECURE	0x0200	/* pw keys in use */
#define	EAPOL_WKEY_INFO_ERROR	0x0400	/* STA found invalid MIC */
#define	EAPOL_WKEY_INFO_REQUEST	0x0800	/* STA requests new key */
#define	EAPOL_WKEY_INFO_ENCRYPT	0x1000	/* encrypted key data (WPA2 only) */
	u_int16_t	ewk_keylen;	/* key length */
	u_int64_t	ewk_replay;	/* replay counter */
	u_int8_t	ewk_nonce[32];
	u_int8_t	ewk_iv[16];	/* initialization vector */
	u_int64_t	ewk_key_rsc;
	u_int64_t	ewk_key_id;
	u_int8_t	ewk_mic[16];
	u_int16_t	ewk_datalen;
	/* variable length data follows */
} __attribute__((__packed__));

/* 
 * EAP packet format as defined in RFC2284.
 */
struct eap_hdr {
	u_int8_t	eap_code;	/* EAP packet kind */
	u_int8_t	eap_id;		/* Request/response identifier */
	u_int16_t	eap_len;	/* Packet length, including header */
	u_int8_t	eap_type;	/* Request/response protocol type */
	/* variable length data follows */
} __attribute__((__packed__));

enum {
	EAP_CODE_REQUEST	= 0x01,
	EAP_CODE_RESPONSE	= 0x02,
	EAP_CODE_SUCCESS	= 0x03,
	EAP_CODE_FAILURE	= 0x04,
};

enum {
	EAP_TYPE_IDENTITY	= 0x01,
	EAP_TYPE_NOTIFICATION	= 0x02,
	EAP_TYPE_NAK		= 0x03,
	EAP_TYPE_MD5_CHALLENGE	= 0x04,
	EAP_TYPE_EAPTLS		= 0x0d,
	EAP_TYPE_LEAP		= 0x11,
};

/*
 * The short form for an EAP packet, used in
 * SUCCESS/FAILURE messages
 */
struct eap_hdr_short {
	u_int8_t	eap_code;	/* EAP packet kind */
	u_int8_t	eap_id;		/* Request/response identifier */
	u_int16_t	eap_len;	/* Packet length, including header */
} __attribute__((__packed__));

#define EAP_IDENTITY_MAXLEN	72	/* max NAI length per RFC2486 */
#endif /* _NET80211_EAPOL_H_ */
