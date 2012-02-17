/*******************************************************************************
 *
 * Name:        mwl_wlanext.c
 * Project:     Linux SoftAP for Marvell CB32/CB35 cardbus adapter
 * Version:     $Revision: 1.1.1.1 $
 * Date:        $Date: 2009/08/19 08:02:21 $
 * Author:      Ralph Roesler (rroesler@syskonnect.de)
 * Purpose:     WLAN extensions functions
 *
 ******************************************************************************/

/*******************************************************************************
 *
 *	(C)Copyright 2004-2005 SysKonnect GmbH.
 *	(C)Copyright 2004-2005 Marvell.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

#include "mwl_ethtool.h"
#include "mwl_version.h"
#include "mwl_main.h"
#include "mwl_wlanext.h"
#include "mwl_hostcmd.h"

/*******************************************************************************
 *
 * Definitions (defines, typedefs, prototypes etc.)
 *
 ******************************************************************************/

static struct iw_statistics *mwl_iw_getstats(struct net_device *);

#define MWL_CHAR_IEEE80211_IOCTL_WRAPPER(name)                             \
static int mwl_ioctl_##name(struct net_device *netdev,                     \
                            struct iw_request_info *info,                  \
                            char *erq,                                     \
                            char *extra)                                   \
{                                                                          \
    struct mwl_private *mwlp = netdev->priv;                               \
    MWL_DBG_INFO(DBG_CLASS_WLEXT, "will call ieee80211 ioctl function");   \
    return ieee80211_ioctl_##name(&mwlp->curr80211com, info, erq, extra);  \
}

#define MWL_POINT_IEEE80211_IOCTL_WRAPPER(name)                            \
static int mwl_ioctl_##name(struct net_device *netdev,                     \
                            struct iw_request_info *info,                  \
                            struct iw_point *erq,                          \
                            char *extra)                                   \
{                                                                          \
    struct mwl_private *mwlp = netdev->priv;                               \
    MWL_DBG_INFO(DBG_CLASS_WLEXT, "will call ieee80211 ioctl function");   \
    return ieee80211_ioctl_##name(&mwlp->curr80211com, info, erq, extra);  \
}

#define MWL_PARAM_IEEE80211_IOCTL_WRAPPER(name)                            \
static int mwl_ioctl_##name(struct net_device *netdev,                     \
                            struct iw_request_info *info,                  \
                            struct iw_param *erq,                          \
                            char *extra)                                   \
{                                                                          \
    struct mwl_private *mwlp = netdev->priv;                               \
    MWL_DBG_INFO(DBG_CLASS_WLEXT, "will call ieee80211 ioctl function");   \
    return ieee80211_ioctl_##name(&mwlp->curr80211com, info, erq, extra);  \
}

#define MWL_SOCKADDR_IEEE80211_IOCTL_WRAPPER(name)                         \
static int mwl_ioctl_##name(struct net_device *netdev,                     \
                            struct iw_request_info *info,                  \
                            struct sockaddr *erq,                          \
                            char *extra)                                   \
{                                                                          \
    struct mwl_private *mwlp = netdev->priv;                               \
    MWL_DBG_INFO(DBG_CLASS_WLEXT, "will call ieee80211 ioctl function");   \
    return ieee80211_ioctl_##name(&mwlp->curr80211com, info, erq, extra);  \
}

#define MWL_FREQ_IEEE80211_IOCTL_WRAPPER(name)                             \
static int mwl_ioctl_##name(struct net_device *netdev,                     \
                            struct iw_request_info *info,                  \
                            struct iw_freq *erq,                           \
                            char *extra)                                   \
{                                                                          \
    struct mwl_private *mwlp = netdev->priv;                               \
    MWL_DBG_INFO(DBG_CLASS_WLEXT, "will call ieee80211 ioctl function");   \
    return ieee80211_ioctl_##name(&mwlp->curr80211com, info, erq, extra);  \
}

#define MWL_U32_IEEE80211_IOCTL_WRAPPER(name)                              \
static int mwl_ioctl_##name(struct net_device *netdev,                     \
                            struct iw_request_info *info,                  \
                            __u32 *erq,                                    \
                            char *extra)                                   \
{                                                                          \
    struct mwl_private *mwlp = netdev->priv;                               \
    MWL_DBG_INFO(DBG_CLASS_WLEXT, "will call ieee80211 ioctl function");   \
    return ieee80211_ioctl_##name(&mwlp->curr80211com, info, erq, extra);  \
}

#define MWL_VOID_IEEE80211_WRAPPER(name)                                   \
static int mwl_ioctl_##name(struct net_device *netdev,                     \
                            struct iw_request_info *info,                  \
                            void *erq,                                     \
                            char *extra)                                   \
{                                                                          \
    struct mwl_private *mwlp = netdev->priv;                               \
    struct ieee80211com *ic = &mwlp->curr80211com;                         \
    int boostmode = ic->ic_boost_mode;                                     \
    int ret = ieee80211_ioctl_##name(&mwlp->curr80211com, info, erq, extra);\
                                                                           \
    if (ic->ic_state == IEEE80211_S_RUN) {                                 \
        if (boostmode != ic->ic_boost_mode) {                              \
            mwl_setFwBoostMode(netdev, ic->ic_boost_mode);                 \
        }                                                                  \
    }                                                                      \
    MWL_DBG_INFO(DBG_CLASS_WLEXT, "called ieee80211 ioctl function");      \
    return ret;                                                            \
}

MWL_CHAR_IEEE80211_IOCTL_WRAPPER(giwname)
MWL_POINT_IEEE80211_IOCTL_WRAPPER(siwencode)
MWL_POINT_IEEE80211_IOCTL_WRAPPER(giwencode)
MWL_PARAM_IEEE80211_IOCTL_WRAPPER(siwrate)
MWL_PARAM_IEEE80211_IOCTL_WRAPPER(giwrate)
MWL_PARAM_IEEE80211_IOCTL_WRAPPER(siwsens)
MWL_PARAM_IEEE80211_IOCTL_WRAPPER(giwsens)
MWL_PARAM_IEEE80211_IOCTL_WRAPPER(siwrts)
MWL_PARAM_IEEE80211_IOCTL_WRAPPER(giwrts)
MWL_PARAM_IEEE80211_IOCTL_WRAPPER(siwfrag)
MWL_PARAM_IEEE80211_IOCTL_WRAPPER(giwfrag)
MWL_SOCKADDR_IEEE80211_IOCTL_WRAPPER(siwap)
MWL_SOCKADDR_IEEE80211_IOCTL_WRAPPER(giwap)
MWL_POINT_IEEE80211_IOCTL_WRAPPER(siwnickn)
MWL_POINT_IEEE80211_IOCTL_WRAPPER(giwnickn)
MWL_FREQ_IEEE80211_IOCTL_WRAPPER(siwfreq)
MWL_FREQ_IEEE80211_IOCTL_WRAPPER(giwfreq)
MWL_POINT_IEEE80211_IOCTL_WRAPPER(siwessid)
MWL_POINT_IEEE80211_IOCTL_WRAPPER(giwessid)
MWL_POINT_IEEE80211_IOCTL_WRAPPER(giwrange)
MWL_U32_IEEE80211_IOCTL_WRAPPER(siwmode)
MWL_U32_IEEE80211_IOCTL_WRAPPER(giwmode)
MWL_PARAM_IEEE80211_IOCTL_WRAPPER(siwpower)
MWL_PARAM_IEEE80211_IOCTL_WRAPPER(giwpower)
MWL_PARAM_IEEE80211_IOCTL_WRAPPER(siwretry)
MWL_PARAM_IEEE80211_IOCTL_WRAPPER(giwretry)
MWL_PARAM_IEEE80211_IOCTL_WRAPPER(siwtxpow)
MWL_PARAM_IEEE80211_IOCTL_WRAPPER(giwtxpow)
MWL_POINT_IEEE80211_IOCTL_WRAPPER(iwaplist)
#ifdef SIOCGIWSCAN
MWL_POINT_IEEE80211_IOCTL_WRAPPER(siwscan)
MWL_POINT_IEEE80211_IOCTL_WRAPPER(giwscan)
#endif
MWL_VOID_IEEE80211_WRAPPER(setparam)
MWL_VOID_IEEE80211_WRAPPER(getparam)
MWL_VOID_IEEE80211_WRAPPER(setkey)
MWL_VOID_IEEE80211_WRAPPER(delkey)
MWL_VOID_IEEE80211_WRAPPER(setmlme)
MWL_VOID_IEEE80211_WRAPPER(setoptie)
MWL_VOID_IEEE80211_WRAPPER(getoptie)
MWL_VOID_IEEE80211_WRAPPER(addmac)
MWL_VOID_IEEE80211_WRAPPER(delmac)
MWL_VOID_IEEE80211_WRAPPER(chanlist)

static const iw_handler mwl_handlers[] = {
    (iw_handler) NULL,                              /* SIOCSIWCOMMIT */
    (iw_handler) mwl_ioctl_giwname,                 /* SIOCGIWNAME   */
    (iw_handler) NULL,                              /* SIOCSIWNWID   */
    (iw_handler) NULL,                              /* SIOCGIWNWID   */
    (iw_handler) mwl_ioctl_siwfreq,                 /* SIOCSIWFREQ   */
    (iw_handler) mwl_ioctl_giwfreq,                 /* SIOCGIWFREQ   */
    (iw_handler) mwl_ioctl_siwmode,                 /* SIOCSIWMODE   */
    (iw_handler) mwl_ioctl_giwmode,                 /* SIOCGIWMODE   */
    (iw_handler) mwl_ioctl_siwsens,                 /* SIOCSIWSENS   */
    (iw_handler) mwl_ioctl_giwsens,                 /* SIOCGIWSENS   */
    (iw_handler) NULL /* not used */,               /* SIOCSIWRANGE  */
    (iw_handler) mwl_ioctl_giwrange,                /* SIOCGIWRANGE  */
    (iw_handler) NULL /* not used */,               /* SIOCSIWPRIV   */
    (iw_handler) NULL /* kernel code */,            /* SIOCGIWPRIV   */
    (iw_handler) NULL /* not used */,               /* SIOCSIWSTATS  */
    (iw_handler) NULL /* kernel code */,            /* SIOCGIWSTATS  */
    (iw_handler) NULL,                              /* SIOCSIWSPY    */
    (iw_handler) NULL,                              /* SIOCGIWSPY    */
    (iw_handler) NULL,                              /* -- hole --    */
    (iw_handler) NULL,                              /* -- hole --    */
    (iw_handler) mwl_ioctl_siwap,                   /* SIOCSIWAP     */
    (iw_handler) mwl_ioctl_giwap,                   /* SIOCGIWAP     */
    (iw_handler) NULL,                              /* -- hole --    */
    (iw_handler) mwl_ioctl_iwaplist,                /* SIOCGIWAPLIST */
#ifdef SIOCGIWSCAN
    (iw_handler) mwl_ioctl_siwscan,                 /* SIOCSIWSCAN   */
    (iw_handler) mwl_ioctl_giwscan,                 /* SIOCGIWSCAN   */
#else
    (iw_handler) NULL,                              /* SIOCSIWSCAN   */
    (iw_handler) NULL,                              /* SIOCGIWSCAN   */
#endif /* SIOCGIWSCAN */
    (iw_handler) mwl_ioctl_siwessid,                /* SIOCSIWESSID  */
    (iw_handler) mwl_ioctl_giwessid,                /* SIOCGIWESSID  */
    (iw_handler) mwl_ioctl_siwnickn,                /* SIOCSIWNICKN  */
    (iw_handler) mwl_ioctl_giwnickn,                /* SIOCGIWNICKN  */
    (iw_handler) NULL,                              /* -- hole --    */
    (iw_handler) NULL,                              /* -- hole --    */
    (iw_handler) mwl_ioctl_siwrate,                 /* SIOCSIWRATE   */
    (iw_handler) mwl_ioctl_giwrate,                 /* SIOCGIWRATE   */
    (iw_handler) mwl_ioctl_siwrts,                  /* SIOCSIWRTS    */
    (iw_handler) mwl_ioctl_giwrts,                  /* SIOCGIWRTS    */
    (iw_handler) mwl_ioctl_siwfrag,                 /* SIOCSIWFRAG   */
    (iw_handler) mwl_ioctl_giwfrag,                 /* SIOCGIWFRAG   */
    (iw_handler) mwl_ioctl_siwtxpow,                /* SIOCSIWTXPOW  */
    (iw_handler) mwl_ioctl_giwtxpow,                /* SIOCGIWTXPOW  */
    (iw_handler) mwl_ioctl_siwretry,                /* SIOCSIWRETRY  */
    (iw_handler) mwl_ioctl_giwretry,                /* SIOCGIWRETRY  */
    (iw_handler) mwl_ioctl_siwencode,               /* SIOCSIWENCODE */
    (iw_handler) mwl_ioctl_giwencode,               /* SIOCGIWENCODE */
    (iw_handler) mwl_ioctl_siwpower,                /* SIOCSIWPOWER  */
    (iw_handler) mwl_ioctl_giwpower,                /* SIOCGIWPOWER  */
};

static const iw_handler mwl_priv_handlers[] = {
    (iw_handler) mwl_ioctl_setparam,                /* SIOCWFIRSTPRIV+ 0 */
    (iw_handler) mwl_ioctl_getparam,                /* SIOCWFIRSTPRIV+ 1 */
    (iw_handler) mwl_ioctl_setkey,                  /* SIOCWFIRSTPRIV+ 2 */
    (iw_handler) NULL,                              /* SIOCWFIRSTPRIV+ 3 */
    (iw_handler) mwl_ioctl_delkey,                  /* SIOCWFIRSTPRIV+ 4 */
    (iw_handler) NULL,                              /* SIOCWFIRSTPRIV+ 5 */
    (iw_handler) mwl_ioctl_setmlme,                 /* SIOCWFIRSTPRIV+ 6 */
    (iw_handler) NULL,                              /* SIOCWFIRSTPRIV+ 7 */
    (iw_handler) mwl_ioctl_setoptie,                /* SIOCWFIRSTPRIV+ 8 */
    (iw_handler) mwl_ioctl_getoptie,                /* SIOCWFIRSTPRIV+ 9 */
    (iw_handler) mwl_ioctl_addmac,                  /* SIOCWFIRSTPRIV+10 */
    (iw_handler) NULL,                              /* SIOCWFIRSTPRIV+11 */
    (iw_handler) mwl_ioctl_delmac,                  /* SIOCWFIRSTPRIV+12 */
    (iw_handler) NULL,                              /* SIOCWFIRSTPRIV+13 */
    (iw_handler) mwl_ioctl_chanlist,                /* SIOCWFIRSTPRIV+14 */
};

static struct iw_handler_def mwl_iw_handler_def = {
    .standard     = (iw_handler *) mwl_handlers,
    .num_standard = sizeof(mwl_handlers) / sizeof(iw_handler),
    .private      = (iw_handler *) mwl_priv_handlers,
    .num_private  = sizeof(mwl_priv_handlers) / sizeof(iw_handler),
};

/*******************************************************************************
 *
 * Global functions
 *
 ******************************************************************************/

int
mwl_setWlanExtOps(struct net_device *netdev)
{
    netdev->get_wireless_stats = mwl_iw_getstats;
    ieee80211_ioctl_iwsetup(&mwl_iw_handler_def);
    netdev->wireless_handlers = &mwl_iw_handler_def;
    return 0;
}

/*******************************************************************************
 *
 * Local functions
 *
 ******************************************************************************/

static struct iw_statistics *
mwl_iw_getstats(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;

    ieee80211_iw_getstats(&mwlp->curr80211com, &mwlp->wStats);
    mwl_getFwStatistics(netdev); 

    mwlp->wStats.discard.fragment += mwlp->privStats.rxFragErrors;
    mwlp->wStats.discard.code     += mwlp->privStats.rxUndecryptFrames  +
                                     mwlp->privStats.rxICVErrors;
    mwlp->wStats.discard.misc     += mwlp->privStats.txFailures         +
                                     mwlp->privStats.RTSFailures        +
                                     mwlp->privStats.ackFailures        +
                                     mwlp->privStats.rxDuplicateFrames  +
                                     mwlp->privStats.FCSErrorCount      +


                                     mwlp->privStats.rxExcludedFrames   +
                                     mwlp->privStats.rxOverflows        +
                                     mwlp->privStats.rxMemErrors        +
                                     mwlp->privStats.pointerErrors      +
                                     mwlp->privStats.txUnderflows;
    return &mwlp->wStats;
}

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
