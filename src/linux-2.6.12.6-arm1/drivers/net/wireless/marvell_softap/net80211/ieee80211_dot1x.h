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
 * $Id: ieee80211_dot1x.h,v 1.1.1.1 2009/08/19 08:02:21 jack Exp $
 */
#ifndef _NET80211_DOT1X_H_
#define _NET80211_DOT1X_H_

/*
 * IEEE 802.1x authenticator.
 */
#include <net80211/eapol.h>

/*
 * Authenticators handle authentication requests from supplicants.
 * Typically this is done by communicating with a backend server
 * using the Radius protocol. When configured in this way an
 * authenticator does little more than pass frames between the
 * supplicant and the backend server.  The exception to this is
 * when authentication is followed by cryptographic key setup; in
 * which case the authenticator must relay the key state and
 * arrange for the keys to be installed prior to passing this
 * information on the supplicant.  This is the basis for 802.1x.
 */

/*
 * Authenticator PAE state machine.
 */
enum {
	EAPOL_AS_INIT		= 0,
	EAPOL_AS_DISCONNECTED	= 1,
	EAPOL_AS_CONNECTING	= 2,
	EAPOL_AS_AUTHENTICATING	= 3,
	EAPOL_AS_AUTHENTICATED	= 4,
	EAPOL_AS_ABORTING	= 5,
	EAPOL_AS_HELD		= 6,
	EAPOL_AS_FORCE_AUTH	= 7,
	EAPOL_AS_FORCE_UNAUTH	= 8,
};

/*
 * Authenticator backend state machine.
 */
enum {
	EAPOL_ABS_INIT		= 0,
	EAPOL_ABS_IDLE		= 1,
	EAPOL_ABS_REQUEST	= 2,
	EAPOL_ABS_RESPONSE	= 3,
	EAPOL_ABS_SUCCESS	= 4,
	EAPOL_ABS_FAIL		= 5,
	EAPOL_ABS_TIMEOUT	= 6,
};

/*
 * Reauthentication timer state machine.
 */
enum {
	EAPOL_ARS_INIT		= 0,
	EAPOL_ARS_REAUTH	= 1,
};

/*
 * Authenticator key transmit state machine.
 */
enum {
	EAPOL_AKS_NO_KEY	= 0,
	EAPOL_AKS_KEY		= 1,
};

/*
 * Port-related definitions.
 */
enum {
	EAPOL_PORTCONTROL_AUTO	= 0,
};
enum {
	EAPOL_PORTMODE_AUTO	= 0,
	EAPOL_PORTMODE_FORCEAUTH= 1,
	EAPOL_PORTMODE_FORCEUNAUTH= 2,
};
enum {
	EAPOL_PORTSTATUS_AUTH	= 0,	/* authenticated */
	EAPOL_PORTSTATUS_UNAUTH	= 1,	/* unauthenticated */
};

struct eapolcom;
struct ieee80211com;
struct ieee80211_node;

/*
 * Base class in case we decide to add supplicant support.
 */
struct eapol_node {
	struct eapolcom		*en_ec;		/* associated authenticator */
	struct ieee80211com	*en_ic;		/* associated device */
	struct ieee80211_node	*en_node;	/* associated 802.11 state */
	u_int8_t		en_id[EAP_IDENTITY_MAXLEN];
	u_int16_t		en_idlen;
};

/*
 * Per-station authentication state.  Variable names are
 * chosen to mimic those used in the 802.1x specification
 * (maybe should group like state into structures).
 */
struct eapol_auth_node {
	struct eapol_node	ean_base;	/* base class */
	u_int			ean_scangen;	/* scan generation # */
	u_int8_t		ean_gone;	/* node needs delayed reclaim */
	struct sk_buff		*ean_skb;	/* from supplicant */

	/* authenticator timers 8.5.2.1 */
	u_int32_t		ean_aWhile;	/* with supplicant or AS */
	u_int32_t		ean_quietWhile;	/* delay before acquiring sup */
	u_int32_t		ean_reAuthWhen;	/* re-authentication interval */
	u_int32_t		ean_txWhen;	/* xmit timeout */
	u_int32_t		ean_reKeyWhen;	/* station re-key timer */
	u_int32_t		ean_gReKeyWhen;	/* group re-key timer */
	/* global variables 8.5.2.2 */
	u_int			ean_authAbort	: 1,
				ean_authFail	: 1,
				ean_authStart	: 1,
				ean_authTimeout	: 1,
				ean_authSuccess	: 1,
				ean_initialize	: 1,
				ean_portControl	: 2,
				ean_portStatus	: 2,
				ean_reAuthenticate : 1;
	u_int8_t		ean_currentId;	/* current id */
	u_int8_t		ean_receivedId;	/* most recent Identifier rx */
	/* authentication variables 8.5.4.1 */
	u_int8_t		ean_authState;
	u_int8_t		ean_eapLogoff	: 1,
				ean_eapStart	: 1,
				ean_portMode	: 2,
				ean_rxRespId	: 1;
	u_int8_t		ean_reAuthCount;
	/* authentication counters 8.5.4.2 */
	u_int			ean_authEntersConnecting;
	u_int			ean_authEapLogoffsWhileConnecting;
	u_int			ean_authEntersAuthenticating;
	u_int			ean_authAuthSuccessesWhileAuthenticating;
	u_int			ean_authAuthTimeoutsWhileAuthenticating;
	u_int			ean_authAuthFailWhileAuthenticating;
	u_int			ean_authAuthReauthsWhileAuthenticating;
	u_int			ean_authAuthEapStartsWhileAuthenticating;
	u_int			ean_authAuthEapLogoffWhileAuthenticating;
	u_int			ean_authAuthReauthsWhileAuthenticated;
	u_int			ean_authAuthEapStartsWhileAuthenticated;
	u_int			ean_authAuthEapLogoffWhileAuthenticated;
	/* key state variables 8.5.5 */
	u_int8_t		ean_keyState;
	u_int			ean_keyAvailable : 1;
	/* reauthentication variables */
	u_int8_t		ean_reAuthState;
	u_int			ean_reAuthPeriod;	/* not part of spec */
	/* backend authentication variables 8.5.8.1.1 */
	u_int8_t		ean_backendState;
	u_int8_t		ean_reqCount;
	u_int8_t		ean_rxResp	: 1,
				ean_aSuccess	: 1,
				ean_aFail	: 1,
				ean_aReq	: 1;
	u_int8_t		ean_idFromServer;
	u_int8_t		ean_reqSrvCount;	/* not part of spec */
	/* backend counters 8.5.8.2 */
	u_int			ean_backendResponses;
	u_int			ean_backendAccessChallenges;
	u_int			ean_backendOtherRequestsToSupplicant;
	u_int			ean_backendNonNakResponsesForSupplicant;
	u_int			ean_backendAuthSuccesses;
	u_int			ean_backendAuthFails;
};
#define	EAPOL_AUTHNODE(_x)	((struct eapol_auth_node *)(_x))

/* write-arounds for base class members */
#define	ean_ec		ean_base.en_ec
#define	ean_ic		ean_base.en_ic
#define	ean_node	ean_base.en_node
#define	ean_id		ean_base.en_id
#define	ean_idlen	ean_base.en_idlen
#define	ean_lock	ean_base.en_lock

struct crypto_tfm;

/*
 * State for each authenticator instance.  We only support
 * one at the moment and it's not clear that more than one
 * per-machine is desirable.
 */
struct eapolcom {
	struct eapol_auth_node	**ec_table;	/* indexed by association id */
	u_int16_t		ec_maxaid;	/* copy of ic_max_aid */
	u_int16_t		ec_inuse;	/* number of entries in use */
	eapol_lock_t		ec_lock;	/* on eapolcom/node table */
	struct timer_list	ec_timer;	/* state machine timers */

	struct crypto_tfm	*ec_md5;

	/* backend state and related methods */
	const struct ieee80211_authenticator_backend *ec_backend;
	struct radiuscom	*ec_radius;
	struct eapol_auth_node	*(*ec_node_alloc)(struct eapolcom *);
	void			(*ec_node_free)(struct eapol_auth_node *);
	void			(*ec_node_reset)(struct eapol_auth_node *);
};

/*
 * Statistics.  We define the radius client statistics
 * here too so only one structure needs to be understood
 * by applications that use it.
 * XXX no way to tell whether a radius client is active
 */
struct eapolstats {
	u_int32_t	eap_badcode;
	u_int32_t	eap_lenmismatch;
	u_int32_t	eap_tooshort;
	u_int32_t	eas_badcode;
	u_int32_t	eas_badidlen;
	u_int32_t	eas_badtype;
	u_int32_t	eas_idmismatch;
	u_int32_t	eak_keynotrequest;
	u_int32_t	eak_badkeytype;
	u_int32_t	eak_replay;
	u_int32_t	eak_nononce;
	u_int32_t	eak_micfailed;
	u_int32_t	eap_keydiscard;
	u_int32_t	eap_keytooshort;
	u_int32_t	eap_keynotwpa;
	u_int32_t	eps_badalloc;
	u_int32_t	eps_badauthfsm;
	u_int32_t	eps_badauthlogoff;
	u_int32_t	eps_badauthstart;
	u_int32_t	eps_badauthtimeout;
	u_int32_t	eps_badbackendfsm;
	u_int32_t	eps_badbackendtimeout;
	u_int32_t	eps_badkeyfsm;
	u_int32_t	eps_badreauthfsm;
	u_int32_t	eps_badtype;
	u_int32_t	eps_badver;
	u_int32_t	eps_noauth;
	u_int32_t	eps_nobuf;
	u_int32_t	eps_noinstance;
	u_int32_t	eps_nonode;
	u_int32_t	eps_nonodemem;
	u_int32_t	eps_nosession;
	u_int32_t	eps_sharecheck;
	u_int32_t	eps_tooshort;
	/* radius server-specific statistics */
	u_int32_t	rs_nosecret;
	u_int32_t	rs_addrmismatch;
	u_int32_t	rs_badattrlen;
	u_int32_t	rs_badcode;
	u_int32_t	rs_badid;
	u_int32_t	rs_badlen;
	u_int32_t	rs_badmode;
	u_int32_t	rs_badmsgauth;
	u_int32_t	rs_badrespauth;
	u_int32_t	rs_cannotbind;
	u_int32_t	rs_emptyreply;
	u_int32_t	rs_lenmismatch;
	u_int32_t	rs_noclone;
	u_int32_t	rs_nomsg;
	u_int32_t	rs_nocrypto;
	u_int32_t	rs_nomem;
	u_int32_t	rs_nomsgauth;
	u_int32_t	rs_nosocket;
	u_int32_t	rs_nothread;
	u_int32_t	rs_request;
	u_int32_t	rs_tooshort;
	u_int32_t	rs_vkeybadsalt;
	u_int32_t	rs_vkeydupsalt;
	u_int32_t	rs_vkeybadvid;
	u_int32_t	rs_vkeybadlen;
	u_int32_t	rs_vkeytooshort;
	u_int32_t	rs_vkeytoolong;
};

#ifdef __linux__
#define	NF_EAPOL	0
#define	NF_EAPOL_OUT	1
#define	NF_EAPOL_IN	2

enum {
	NET_8021X	= 18,			/* XXX */
};
#endif

#if defined(__KERNEL__) || defined(_KERNEL)
extern	void eapol_fsm_run(struct eapol_auth_node *);
extern	void eapol_reauth_setperiod(struct eapol_auth_node *, int timeout);
extern	void eapol_send_raw(struct eapol_node *, struct sk_buff *);
extern	void eapol_send(struct eapol_node *, struct sk_buff *, u_int8_t type);
extern	struct sk_buff *eapol_alloc_skb(u_int payload);
extern	void eapol_hmac_md5(struct eapolcom *ec, void *data, u_int datalen,
		void *key, u_int keylen, u_int8_t hash[16]);

extern	struct eapolstats eapolstats;

MALLOC_DECLARE(M_EAPOL_NODE);
#endif

#endif /* _NET80211_DOT1X_H_ */
