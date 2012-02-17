/*******************************************************************************
 *
 * Name:        mwl_ethtool.c
 * Project:     Linux SoftAP for Marvell CB32/CB35 cardbus adapter
 * Version:     $Revision: 1.1.1.1 $
 * Date:        $Date: 2009/08/19 08:02:21 $
 * Author:      Ralph Roesler (rroesler@syskonnect.de)
 * Purpose:     ethtool functions
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
#include "mwl_hostcmd.h"

/*******************************************************************************
 *
 * Definitions (defines, typedefs, prototypes etc.)
 *
 ******************************************************************************/

#ifndef ETHT_STATSTRING_LEN
#define ETHT_STATSTRING_LEN 32
#endif

#define MWL_STAT(m) sizeof(((struct mwl_private *)0)->m), offsetof(struct mwl_private, m)

static void mwl_getDrvInfo(struct net_device *, struct ethtool_drvinfo *);
static int mwl_getStatsCount(struct net_device *);
static void mwl_getStrings(struct net_device *, uint32_t, uint8_t *);
static void mwl_getEthtoolStats(struct net_device *, struct ethtool_stats *, uint64_t *);

struct mwl_stats {
    char  stat_string[ETH_GSTRING_LEN];
    int   sizeof_stat;
    int   stat_offset;
};

static const struct mwl_stats mwl_gstrings_stats[] = {
    { "rx_packets", MWL_STAT(netDevStats.rx_packets) },
    { "tx_packets", MWL_STAT(netDevStats.tx_packets) },
    { "rx_bytes", MWL_STAT(netDevStats.rx_bytes) },
    { "tx_bytes", MWL_STAT(netDevStats.tx_bytes) },
    { "rx_errors", MWL_STAT(netDevStats.rx_errors) },
    { "tx_errors", MWL_STAT(netDevStats.tx_errors) },
    { "rx_dropped", MWL_STAT(netDevStats.rx_dropped) },
    { "tx_dropped", MWL_STAT(netDevStats.tx_dropped) },
    { "multicast", MWL_STAT(netDevStats.multicast) },
    { "collisions", MWL_STAT(netDevStats.collisions) },
    { "rx_length_errors", MWL_STAT(netDevStats.rx_length_errors) },
    { "rx_over_errors", MWL_STAT(netDevStats.rx_over_errors) },
    { "rx_crc_errors", MWL_STAT(netDevStats.rx_crc_errors) },
    { "rx_frame_errors", MWL_STAT(netDevStats.rx_frame_errors) },
    { "rx_fifo_errors", MWL_STAT(netDevStats.rx_fifo_errors) },
    { "rx_missed_errors", MWL_STAT(netDevStats.rx_missed_errors) },
    { "tx_aborted_errors", MWL_STAT(netDevStats.tx_aborted_errors) },
    { "tx_carrier_errors", MWL_STAT(netDevStats.tx_carrier_errors) },
    { "tx_fifo_errors", MWL_STAT(netDevStats.tx_fifo_errors) },
    { "tx_heartbeat_errors", MWL_STAT(netDevStats.tx_heartbeat_errors) },
    { "tx_window_errors", MWL_STAT(netDevStats.tx_window_errors) },
    { "fw_tx_retry_success", MWL_STAT(privStats.txRetrySuccesses) },
    { "fw_tx_multi_retry_success", MWL_STAT(privStats.txMultRetrySucc) },
    { "fw_tx_failures", MWL_STAT(privStats.txFailures) },
    { "fw_tx_rts_success", MWL_STAT(privStats.RTSSuccesses) },
    { "fw_tx_rts_failures", MWL_STAT(privStats.RTSFailures) },
    { "fw_tx_ack_failures", MWL_STAT(privStats.ackFailures) },
    { "fw_tx_watchdog_timeouts", MWL_STAT(privStats.txWatchDogTimeouts) },
    { "fw_tx_underflows", MWL_STAT(privStats.txUnderflows) },
    { "fw_tx_done", MWL_STAT(privStats.txDone) },
    { "fw_tx_done_buf_try_put", MWL_STAT(privStats.txDoneBufTryPut) },
    { "fw_tx_done_buf_put", MWL_STAT(privStats.txDoneBufPut) },
    { "fw_tx_wait_for_buffer", MWL_STAT(privStats.wait4TxBuf) },
    { "fw_tx_attempts", MWL_STAT(privStats.txAttempts) },
    { "fw_tx_successes", MWL_STAT(privStats.txSuccesses) },
    { "fw_tx_fragments", MWL_STAT(privStats.txFragments) },
    { "fw_tx_multicasts", MWL_STAT(privStats.txMulticasts) },
    { "fw_rx_duplicate_frames", MWL_STAT(privStats.rxDuplicateFrames) },
    { "fw_rx_fcs_errors", MWL_STAT(privStats.FCSErrorCount) },
    { "fw_rx_overflows", MWL_STAT(privStats.rxOverflows) },
    { "fw_rx_frag_errors", MWL_STAT(privStats.rxFragErrors) },
    { "fw_rx_memory_errors", MWL_STAT(privStats.rxMemErrors) },
    { "fw_rx_pointer_errors", MWL_STAT(privStats.pointerErrors) },
    { "fw_rx_non_control_packets", MWL_STAT(privStats.rxNonCtlPkts) },
    { "fw_rx_multicasts", MWL_STAT(privStats.rxMulticasts) },
    { "fw_rx_undecrypted_frames", MWL_STAT(privStats.rxUndecryptFrames) },
    { "fw_rx_icv_errors", MWL_STAT(privStats.rxICVErrors) },
    { "fw_rx_excluded_frames", MWL_STAT(privStats.rxExcludedFrames) }
};

#ifdef SET_ETHTOOL_OPS
static struct ethtool_ops mwl_ethtoolOps = {
    .get_drvinfo       = mwl_getDrvInfo,
    .get_strings       = mwl_getStrings,
    .get_stats_count   = mwl_getStatsCount,
    .get_ethtool_stats = mwl_getEthtoolStats,
};
#endif

/*******************************************************************************
 *
 * Global functions
 *
 ******************************************************************************/

int
mwl_setEthtoolOps(struct net_device *netdev)
{
#ifdef SET_ETHTOOL_OPS
    SET_ETHTOOL_OPS(netdev, &mwl_ethtoolOps);
#endif
    return 0;
}

int 
mwl_doEthtoolIoctl(struct ieee80211com *ic, struct ifreq *ifr, int cmd)
{
    void *userdata = (void *) ifr->ifr_data;
    struct net_device *netdev = ic->ic_dev;
    int error = -EFAULT;
    u32 ethcmd;

    if (get_user(ethcmd, (u32 *) userdata)) {
        return error;
    }

    switch(ethcmd) {
        case ETHTOOL_GDRVINFO: {
                struct ethtool_drvinfo drvinfo = { ETHTOOL_GDRVINFO };
                mwl_getDrvInfo(netdev, &drvinfo);
                if (!copy_to_user(userdata, &drvinfo, sizeof(drvinfo))) {
                    error = 0; /* everything went fine */
                }
            }
            break;
        case ETHTOOL_GSTRINGS: {
                struct ethtool_gstrings estr = { ETHTOOL_GSTRINGS };
                int numStrBytes = mwl_getStatsCount(netdev)*ETHT_STATSTRING_LEN;
                estr.len = mwl_getStatsCount(netdev);
                if (!copy_from_user(&estr, userdata, sizeof(estr))) {
                    if (estr.string_set == ETH_SS_STATS) {
                        char *strings = kmalloc(numStrBytes, GFP_KERNEL);
                        if (strings != NULL) {
                            mwl_getStrings(netdev, ETH_SS_STATS, strings);
                            if (!copy_to_user(userdata, &estr, sizeof(estr))) {
                                if (!copy_to_user(userdata + sizeof(estr),
                                                  strings, numStrBytes)) {
                                    error = 0; /* everything went fine */
                                }
                            }
                            kfree(strings);
                        }
                    }
                }
            }
            break;
        case ETHTOOL_GSTATS: {
                struct ethtool_stats estats = { ETHTOOL_GSTATS };
                int numStatBytes = mwl_getStatsCount(netdev) * sizeof(u64);
                uint64_t *stats = kmalloc(numStatBytes, GFP_KERNEL);
                estats.n_stats = mwl_getStatsCount(netdev);
                if (stats != NULL) {
                    mwl_getEthtoolStats(netdev, &estats, stats);
                    if (!copy_to_user(userdata, &estats, sizeof(estats))) {
                        if (!copy_to_user(userdata + sizeof(estats), 
                                          stats, numStatBytes)) {
                            error = 0; /* everything went fine */
                        }
                    }
                    kfree(stats);
                }
            }
            break;
        default:
            error = -EOPNOTSUPP;
            break;
    } /* end switch(ethcmd) */
    return error;
}

/*******************************************************************************
 *
 * Local functions
 *
 ******************************************************************************/

static void 
mwl_getDrvInfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
    struct mwl_private *mwlp = netdev->priv;

    drvinfo->n_stats = sizeof(mwl_gstrings_stats) / sizeof(struct mwl_stats);
    strncpy(drvinfo->driver, DRV_NAME ,sizeof(drvinfo->driver)-1);
    strncpy(drvinfo->version, DRV_VERSION, sizeof(drvinfo->version)-1);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,22)
    strncpy(drvinfo->bus_info, pci_name((struct pci_dev *)mwlp->pPciDev),
                sizeof(drvinfo->bus_info)-1);
#endif
    snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version)-1, "%i.%i.%i.%i",
             (mwlp->hwData.fwReleaseNumber & 0xff000000) >> 24,
             (mwlp->hwData.fwReleaseNumber & 0x00ff0000) >> 16,
             (mwlp->hwData.fwReleaseNumber & 0x0000ff00) >>  8,
             (mwlp->hwData.fwReleaseNumber & 0x000000ff));
}

static int
mwl_getStatsCount(struct net_device *netdev)
{
    return (sizeof(mwl_gstrings_stats) / sizeof(struct mwl_stats));
}

static void
mwl_getEthtoolStats(struct net_device *netdev, struct ethtool_stats *stats, uint64_t *data)
{
    struct mwl_private *mwlp = netdev->priv;
    int maxStatEntries = sizeof(mwl_gstrings_stats)/sizeof(struct mwl_stats);
    int currStatEntry;

    mwl_getFwStatistics(netdev);
    for (currStatEntry=0; currStatEntry < maxStatEntries; currStatEntry++) {
        char *p = (char *)mwlp+mwl_gstrings_stats[currStatEntry].stat_offset;
        data[currStatEntry] = 
            (mwl_gstrings_stats[currStatEntry].sizeof_stat ==
                   sizeof(uint64_t)) ? *(uint64_t *)p : *(uint32_t *)p;
    }
}

static void
mwl_getStrings(struct net_device *netdev, uint32_t stringset, uint8_t *data)
{
    int maxStatEntries = sizeof(mwl_gstrings_stats)/sizeof(struct mwl_stats);
    int currStatEntry;

    if (stringset == ETH_SS_STATS) {
        for (currStatEntry=0; currStatEntry < maxStatEntries; currStatEntry++) {
            memcpy(data + currStatEntry * ETH_GSTRING_LEN,
                   mwl_gstrings_stats[currStatEntry].stat_string,
                   ETH_GSTRING_LEN);
        }
    }
}

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
