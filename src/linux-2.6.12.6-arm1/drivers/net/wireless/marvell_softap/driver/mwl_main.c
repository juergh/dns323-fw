/*******************************************************************************
 *
 * Name:        mwl_main.c
 * Project:     Linux SoftAP for Marvell CB32/CB35 cardbus adapter
 * Version:     $Revision: 1.1.1.1 $
 * Date:        $Date: 2009/08/19 08:02:21 $
 * Author:      Ralph Roesler (rroesler@syskonnect.de)
 * Purpose:     main functions
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

#include "mwl_debug.h"
#include "mwl_ethtool.h"
#include "mwl_download.h"
#include "mwl_registers.h"
#include "mwl_version.h"
#include "mwl_descriptors.h"
#include "mwl_hostcmd.h"
#include "mwl_xmitrecv.h"
#include "mwl_main.h"

/*******************************************************************************
 *
 * Definitions (defines, typedefs, prototypes etc.)
 *
 ******************************************************************************/

static int mwl_open(struct net_device *);
static int mwl_stop(struct net_device *);
static void mwl_setMcList(struct net_device *);
static int mwl_ioctl(struct net_device *, struct ifreq *, int);
static struct net_device_stats *mwl_getStats(struct net_device *);
static int mwl_setMacAddr(struct net_device *, void *);
static int mwl_changeMtu(struct net_device *, int);
static int mwl_reset(struct net_device *);
static int mwl_reloadFirmwareAndInitDescriptors(struct net_device *);
static void mwl_txTimeout(struct net_device *);
static void mwl_nextScan(unsigned long);
static int mwl_setupChannels(struct net_device *);
static int mwl_setupRates(struct net_device *);
static int mwl_channelSet(struct net_device *, struct ieee80211_channel *);
static struct ieee80211_node *mwl_nodeAlloc(struct ieee80211com *);
static void mwl_nodeCopy(struct ieee80211com *, struct ieee80211_node *, const struct ieee80211_node *);
static void mwl_nodeCleanup(struct ieee80211com *, struct ieee80211_node *);
static u_int8_t mwl_nodeGetRssi(struct ieee80211com *, struct ieee80211_node *);
static int mwl_newState(struct ieee80211com *, enum ieee80211_state, int);
static int mwl_mediaChange(struct net_device *);
static void mwl_keyConfig(struct ieee80211com *ic, mwl_operation_e action);
static void mwl_keyUpdateBegin(struct ieee80211com *);
static int mwl_keyAlloc(struct ieee80211com *, const struct ieee80211_key *);
static int mwl_keyDelete(struct ieee80211com *, const struct ieee80211_key *);
static int mwl_keySet(struct ieee80211com *, const struct ieee80211_key *, const u_int8_t *);
static void mwl_keyUpdateEnd(struct ieee80211com *);
static void mwl_newAssoc(struct ieee80211com *, struct ieee80211_node *, int);
static int mwl_setSlot(struct net_device *);
static void mwl_updateSlot(struct net_device *);
static int mwl_beaconAlloc(struct net_device *, struct ieee80211_node *, mwl_bool_e);
static int mwl_beaconSetup(struct net_device *);
static void mwl_beaconConfig(struct net_device *);
static int mwl_rateMbpsToRateIdx(mwl_rate_e);
static int mwl_rateIdxToRateMbps(int);

static mwl_bool_e isInterfaceUsed = MWL_FALSE;

static u_int8_t ratesBmode[] = { MWL_RATE_1_0MBPS  | 0x80,
                                 MWL_RATE_2_0MBPS  | 0x80,
                                 MWL_RATE_5_5MBPS  | 0x80,
                                 MWL_RATE_11_0MBPS | 0x80 };

static u_int8_t ratesGmode[] = { MWL_RATE_1_0MBPS  | 0x80,
                                 MWL_RATE_2_0MBPS  | 0x80,
                                 MWL_RATE_5_5MBPS  | 0x80,
                                 MWL_RATE_11_0MBPS | 0x80,
                                 MWL_RATE_6_0MBPS,
                                 MWL_RATE_9_0MBPS,
                                 MWL_RATE_12_0MBPS,
                                 MWL_RATE_18_0MBPS,
                                 MWL_RATE_24_0MBPS,
                                 MWL_RATE_36_0MBPS,
                                 MWL_RATE_48_0MBPS,
                                 MWL_RATE_54_0MBPS };

typedef enum {
    IEEE80211_ERROR = 0,
    IEEE80211_OK = 1,
} ieee80211_result_e;

#define EOK                 0    /* successful return code for Linux */
#define MWL_MAX_MTU         2290 /* MAXLEN-WEP-QOS-RSN/WPA: 2312-8-2-12=2290 */
#define MWL_MIN_MTU         32
#define CMD_BUF_SIZE        0x4000
#define MAX_RCV_ITERATION   10
#define MAX_ISR_ITERATION   1 // 10

#define USE_SHPREAMBLE(_ic) \
    (((_ic)->ic_flags & (IEEE80211_F_SHPREAMBLE | \
                         IEEE80211_F_USEBARKER)) == IEEE80211_F_SHPREAMBLE)

#ifndef __MOD_INC_USE_COUNT
#define MWL_MOD_INC_USE(_m, _err)                                     \
    if (isInterfaceUsed == MWL_FALSE) {                               \
        isInterfaceUsed = MWL_TRUE;                                   \
        if (!try_module_get(_m)) {                                    \
            printk(KERN_WARNING "%s: try_module_get?!?\n", __func__); \
            _err;                                                     \
        }                                                             \
    }
#define MWL_MOD_DEC_USE(_m)                                           \
    if (isInterfaceUsed == MWL_TRUE) {                                \
        isInterfaceUsed = MWL_FALSE;                                  \
        module_put(_m);                                               \
    }
#else
#define MWL_MOD_INC_USE(_m, _err)                                     \
    if (isInterfaceUsed == MWL_FALSE) {                               \
        isInterfaceUsed = MWL_TRUE;                                   \
        MOD_INC_USE_COUNT;                                            \
    }
#define MWL_MOD_DEC_USE(_m)                                           \
    if (isInterfaceUsed == MWL_TRUE) {                                \
        isInterfaceUsed = MWL_FALSE;                                  \
        MOD_DEC_USE_COUNT;                                            \
    }
#endif

/*******************************************************************************
 *
 * Global functions
 *
 ******************************************************************************/

int
mwl_init(struct net_device *netdev, u_int16_t devid)
{
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;
    int retCode;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    spin_lock_init(&mwlp->locks.xmitLock);
    spin_lock_init(&mwlp->locks.fwLock);

    mwlp->pCmdBuf = (unsigned short *) 
          pci_alloc_consistent(mwlp->pPciDev, CMD_BUF_SIZE, &mwlp->pPhysCmdBuf);
    if (mwlp->pCmdBuf == NULL) {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "no mem");
        return MWL_ERROR;
    }
    memset(mwlp->pCmdBuf, 0x00, CMD_BUF_SIZE);

    ether_setup(netdev); /* init eth data structures */

    if ((retCode = mwl_allocateTxRing(netdev)) == 0) {
        if ((retCode = mwl_initTxRing(netdev)) != 0) {
            MWL_DBG_ERROR(DBG_CLASS_MAIN, "could not initialize TX ring");
            goto err_init;
        }
    } else {
        MWL_DBG_ERROR(DBG_CLASS_MAIN, "could not allocate TX ring");
        goto err_init;
    }

    if ((retCode = mwl_allocateRxRing(netdev)) == 0) {
        if ((retCode = mwl_initRxRing(netdev)) != 0) {
            MWL_DBG_ERROR(DBG_CLASS_MAIN, "could not initialize RX ring");
            goto err_init;
        }
    } else {
        MWL_DBG_ERROR(DBG_CLASS_MAIN, "could not allocate RX ring");
        goto err_init;
    }

    if (mwl_downloadFirmware(netdev, IEEE80211_M_HOSTAP)) {
        MWL_DBG_ERROR(DBG_CLASS_MAIN, "could not download FW");
        goto err_init;
    }

    if (mwl_getFwHardwareSpecs(netdev)) {
        MWL_DBG_ERROR(DBG_CLASS_MAIN, "could not get HW specs");
        goto err_init;
    }

    if (mwl_setupChannels(netdev)) {
        MWL_DBG_ERROR(DBG_CLASS_MAIN, "could not setup channels");
        goto err_init;
    }

    if (mwl_setupRates(netdev)) {
        MWL_DBG_ERROR(DBG_CLASS_MAIN, "could not setup rates");
        goto err_init;
    }

    writel((mwlp->descData.pPhysTxRing),
            mwlp->ioBase0 + mwlp->descData.wcbBase);
    writel((mwlp->descData.pPhysRxRing),
            mwlp->ioBase0 + mwlp->descData.rxDescRead);
    writel((mwlp->descData.pPhysRxRing),
            mwlp->ioBase0 + mwlp->descData.rxDescWrite);

    init_timer(&mwlp->neighborScanTimer);
    mwlp->neighborScanTimer.function = mwl_nextScan;
    mwlp->neighborScanTimer.data = (unsigned long) netdev;

    netdev->open            = mwl_open;
    netdev->stop            = mwl_stop;
    netdev->hard_start_xmit = mwl_dataXmit;
    netdev->tx_timeout        = mwl_txTimeout;
    netdev->watchdog_timeo     = 30 * HZ;
    netdev->set_multicast_list = mwl_setMcList;
    netdev->do_ioctl           = mwl_ioctl;
    netdev->get_stats          = mwl_getStats;
    netdev->set_mac_address    = mwl_setMacAddr;
    netdev->change_mtu         = mwl_changeMtu;

    mwl_setEthtoolOps(netdev);
    mwl_setWlanExtOps(netdev);

    // ic->msg_enable = 0xffffffff; /* debug messages: everything */
    ic->ic_dev        = netdev;
    ic->ic_devstats   = &mwlp->netDevStats;
    ic->ic_mgtstart   = mwl_mgmtXmit;
    ic->ic_recv_mgmt  = mwl_mgmtRecv;
    ic->ic_init       = mwl_open;
    ic->ic_reset      = mwl_reset;
    ic->ic_newassoc   = mwl_newAssoc;
    ic->ic_updateslot = mwl_updateSlot;
    ic->ic_phytype    = IEEE80211_T_OFDM;
    ic->ic_opmode     = IEEE80211_M_HOSTAP; 
    ic->ic_curmode    = IEEE80211_MODE_11G;
    ic->ic_caps       = IEEE80211_C_IBSS    | 
                        IEEE80211_C_HOSTAP  | 
                        IEEE80211_C_MONITOR | 
                        IEEE80211_C_TXPMGT  | /* transmit power control */
                        IEEE80211_C_SHSLOT  |
                        IEEE80211_C_AES_CCM |
                        IEEE80211_C_WEP     | /* WEP encryption in HW */
                        IEEE80211_C_TKIPMIC |
                        IEEE80211_C_TKIP;    

    ic->ic_flags     |= IEEE80211_F_SHSLOT;   /* use initially short slot */
    ic->ic_flags     |= IEEE80211_F_DATAPAD;  /* 802.11 header aligned 32bit */
    mwlp->initialOpmode = ic->ic_opmode;
    mwlp->fixedRate     = -1; /* AUTO_SELECT (rate adaption) */

    IEEE80211_ADDR_COPY(ic->ic_myaddr, mwlp->hwData.macAddr);
    IEEE80211_ADDR_COPY(netdev->dev_addr, mwlp->hwData.macAddr);
    ieee80211_ifattach(ic);
    mwlp->parentFuncs.mwl_nodeCleanup = ic->ic_node_cleanup;
    mwlp->parentFuncs.mwl_nodeCopy    = ic->ic_node_copy;
    mwlp->parentFuncs.mwl_mgmtRecv    = ic->ic_recv_mgmt;
    mwlp->parentFuncs.mwl_newState    = ic->ic_newstate;
    ic->ic_node_alloc                 = mwl_nodeAlloc;
    ic->ic_node_cleanup               = mwl_nodeCleanup;
    ic->ic_node_copy                  = mwl_nodeCopy;
    ic->ic_node_getrssi               = mwl_nodeGetRssi;
    ic->ic_newstate                   = mwl_newState;
    ic->ic_recv_mgmt                  = mwl_mgmtRecv;
    ic->ic_crypto.cs_key_alloc        = mwl_keyAlloc;
    ic->ic_crypto.cs_key_delete       = mwl_keyDelete;
    ic->ic_crypto.cs_key_set          = mwl_keySet;
    ic->ic_crypto.cs_key_update_begin = mwl_keyUpdateBegin;
    ic->ic_crypto.cs_key_update_end   = mwl_keyUpdateEnd;

    ieee80211_media_init(ic, mwl_mediaChange, ieee80211_media_status);

    if (register_netdev(netdev)) {
        printk(KERN_ERR "%s: unable to register device\n", DRV_NAME);
        goto err_register_netdev;
    }
 
    ieee80211_announce(ic);
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
    return MWL_OK;

err_register_netdev:
    ieee80211_ifdetach(ic);
err_init:
    mwl_cleanRxRing(netdev);
    mwl_freeRxRing(netdev);
    mwl_cleanTxRing(netdev);
    mwl_freeTxRing(netdev);
    pci_free_consistent(mwlp->pPciDev, CMD_BUF_SIZE,
                        mwlp->pCmdBuf, mwlp->pPhysCmdBuf);
    MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, NULL);
    return MWL_ERROR;
}

int
mwl_deinit(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    if (netdev->flags & IFF_RUNNING) {
        if (mwl_stop(netdev)) {
            printk(KERN_ERR "%s: unable to stop device\n", DRV_NAME);
        }
    }
    ieee80211_ifdetach(ic);
    mwl_disableInterrupts(netdev);
    mwl_resetAdapterAndVanishFw(netdev);
    mwl_cleanRxRing(netdev);
    mwl_freeRxRing(netdev);
    mwl_cleanTxRing(netdev);
    mwl_freeTxRing(netdev);
    unregister_netdev(netdev);
    pci_free_consistent(mwlp->pPciDev, CMD_BUF_SIZE,
                        (caddr_t) mwlp->pCmdBuf, mwlp->pPhysCmdBuf);
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
    return MWL_OK;
}

irqreturn_t
mwl_isr(int irq, void *dev_id, struct pt_regs *regs)
{
    struct net_device *netdev = (struct net_device *) dev_id;
    struct mwl_private *mwlp = netdev->priv;
    unsigned int currIteration = 0;
    unsigned int intStatus;
    unsigned int dummy;
#ifdef MWL_KERNEL_26
    irqreturn_t retVal = IRQ_NONE;
#else
    int retVal = IRQ_NONE;
#endif

    do {
        intStatus = (readl(mwlp->ioBase1 + MACREG_REG_A2H_INTERRUPT_CAUSE));
        if (intStatus != 0x00000000) {
            if (intStatus == 0xffffffff) {
                MWL_DBG_INFO(DBG_CLASS_MAIN, "card plugged out???");
                retVal = IRQ_HANDLED;
                break; /* card plugged out -> do not handle any IRQ */
            }
            writel((MACREG_A2HRIC_BIT_MASK & ~intStatus),
                mwlp->ioBase1 + MACREG_REG_A2H_INTERRUPT_CAUSE);
            dummy = readl(mwlp->ioBase1 + MACREG_REG_INT_CODE);
        }
        if ((intStatus & ISR_SRC_BITS) || (currIteration < MAX_ISR_ITERATION)) {
            if (intStatus & MACREG_A2HRIC_BIT_TX_DONE) {
                intStatus &= ~MACREG_A2HRIC_BIT_TX_DONE;
                mwl_xmitComplete(netdev);
                retVal = IRQ_HANDLED;
            }
            if (intStatus & MACREG_A2HRIC_BIT_RX_RDY) {
                intStatus &= ~MACREG_A2HRIC_BIT_RX_RDY;
                mwl_recv(netdev);
                retVal = IRQ_HANDLED;
            }
            if (intStatus & MACREG_A2HRIC_BIT_OPC_DONE) {
                intStatus &= ~MACREG_A2HRIC_BIT_OPC_DONE;
                mwl_fwCommandComplete(netdev);
                retVal = IRQ_HANDLED;
            }
            if (intStatus & MACREG_A2HRIC_BIT_MAC_EVENT) {
                intStatus &= ~MACREG_A2HRIC_BIT_MAC_EVENT;
                retVal = IRQ_HANDLED;
            }
        }
        currIteration++;
    } while (currIteration < MAX_ISR_ITERATION);

#ifdef MWL_KERNEL_26
    return retVal;
#else
    return;
#endif
}

void
mwl_enableInterrupts(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    unsigned int dummy;

    if (mwl_isAdapterPluggedIn(netdev)) {
        writel(0x00, mwlp->ioBase1 + MACREG_REG_A2H_INTERRUPT_MASK);
        dummy = readl(mwlp->ioBase1 + MACREG_REG_INT_CODE);

        writel((MACREG_A2HRIC_BIT_MASK), 
            mwlp->ioBase1 + MACREG_REG_A2H_INTERRUPT_MASK);
        dummy = readl(mwlp->ioBase1 + MACREG_REG_INT_CODE);
    }
}

void
mwl_disableInterrupts(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    unsigned int dummy;

    if (mwl_isAdapterPluggedIn(netdev)) {
        writel(0x00, mwlp->ioBase1 + MACREG_REG_A2H_INTERRUPT_MASK);
        dummy = readl(mwlp->ioBase1 + MACREG_REG_INT_CODE);
    }
}

void
mwl_resetAdapterAndVanishFw(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;

    if (mwl_isAdapterPluggedIn(netdev)) {
        writel(ISR_RESET, mwlp->ioBase1 + MACREG_REG_H2A_INTERRUPT_EVENTS);
    }
}

int
mwl_isAdapterPluggedIn(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    u_int32_t regval;

    regval = readl(mwlp->ioBase1 + MACREG_REG_INT_CODE);
    if (regval == 0xffffffff) {
        return MWL_ADAPTER_NOT_PLUGGED;
    }
    return MWL_ADAPTER_PLUGGED_IN;
}

/*******************************************************************************
 *
 * Local functions
 *
 ******************************************************************************/

static int
mwl_open(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;
    struct ieee80211_node *ni = ic->ic_bss;
    mwl_facilitate_e radioOnOff = (ni->ni_txpower) ? MWL_ENABLE : MWL_DISABLE;
    enum ieee80211_phymode mode;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    /* clean up statistics */
    memset(&mwlp->netDevStats, 0x00, sizeof(struct net_device_stats));
    memset(&mwlp->privStats, 0x00, sizeof(struct mwl_priv_stats));

    /* Change our interface type if we are in monitor mode. */
    netdev->type = (ic->ic_opmode == IEEE80211_M_MONITOR) ?
                ARPHRD_IEEE80211_PRISM : ARPHRD_ETHER;

    /* protect driver from handling any xmit ico. open from ieee802.11 */
    if (netdev->flags & IFF_RUNNING) {
        netif_stop_queue(netdev);
        netdev->flags &= ~IFF_RUNNING;
        mwl_disableInterrupts(netdev);
    }

    /* Make sure we got the correct FW on the adapter card */
    if (ic->ic_opmode != mwlp->initialOpmode) {
        if ((ic->ic_opmode       == IEEE80211_M_HOSTAP) || 
            (mwlp->initialOpmode == IEEE80211_M_HOSTAP)) {
            if (mwl_reloadFirmwareAndInitDescriptors(netdev)) {
                MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "loading FW");
                return -EFAULT;
            } else {
                mwlp->initialOpmode = ic->ic_opmode;
            }
        }
    } else {
        if (netdev->flags & IFF_RUNNING) {
            if (mwl_reloadFirmwareAndInitDescriptors(netdev)) {
                MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "loading FW");
                return -EFAULT;
            }
        }
    }

    mwlp->currChannel = *(ic->ic_ibss_chan);
    if (mwl_setFwAntenna(netdev, MWL_ANTENNATYPE_RX)) {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting RX antenna");
        return -EIO;
    }
    if (mwl_setFwAntenna(netdev, MWL_ANTENNATYPE_TX)) {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting TX antenna");
        return -EIO;
    }
    if (mwl_setFwRadio(netdev, radioOnOff, MWL_AUTO_PREAMBLE)) {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting auto preamble");
        return -EIO;
    }
    if (mwl_setFwTxPower(netdev, ni->ni_txpower)) {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting TX power");
        return -EIO;
    }
    if (mwl_setFwRTSThreshold(netdev, ic->ic_rtsthreshold)) {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting RTS threshold");
        return -EIO;
    }
    if (ic->ic_boost_mode != MWL_BOOST_MODE_REGULAR) {
        if (mwl_setFwBoostMode(netdev, ic->ic_boost_mode)) {
            MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting boost mode");
            return -EIO;
        }
    }

    mwl_enableInterrupts(netdev);
    netdev->flags |= IFF_RUNNING;

    /* 
    ** The hardware should be ready to go now so it's safe to kick the 
    ** 802.11 state machine as it's likely to immediately call back to
    ** us to send mgmt frames.
    */
    ni = ic->ic_bss;
    ni->ni_chan = ic->ic_ibss_chan;
    ic->ic_state = IEEE80211_S_INIT;
    mode = ieee80211_chan2mode(ic, ni->ni_chan);

    if (ic->ic_opmode != IEEE80211_M_MONITOR) {
        if (ic->ic_roaming != IEEE80211_ROAMING_MANUAL) {
            if (ic->ic_opmode == IEEE80211_M_IBSS) {
                ic->ic_des_chan = &ic->ic_channels[1];
            } 
            ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
        }
    } else {
        ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
    }

    MWL_MOD_INC_USE(THIS_MODULE, return -EIO);
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
    return EOK;
}

static int
mwl_stop(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    if (netdev->flags & IFF_RUNNING) {
        /* 
        ** Shutdown the hardware and driver:                  
        **  a) reset 802.11 state machine (do first so       
        **      station deassoc/deauth frames can be sent)  
        **  b)  stop output from above                     
        **  c)  disable interrupts                        
        **  d)  turn off timers                          
        **  e)  turn off the radio                      
        */
        ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
        netif_stop_queue(netdev);
        netdev->flags &= ~IFF_RUNNING;
        if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
            if (mwl_setFwApBss(netdev, MWL_DISABLE)) {
                MWL_DBG_WARN(DBG_CLASS_MAIN, "disabling AP bss");
            }
        }
        if (mwl_setFwRadio(netdev, MWL_DISABLE, MWL_AUTO_PREAMBLE)) {
            MWL_DBG_WARN(DBG_CLASS_MAIN, "disabling auto preamble");
        }
        mwl_disableInterrupts(netdev);
        /* 
        ** free the finalized joined beacon information as well,
        ** so that it can be assigned again later... 
        ** mwl_beaconFree(netdev); ??
        */
        mwlp->usingGProtection = MWL_DISABLE;
    }

    MWL_MOD_DEC_USE(THIS_MODULE);
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
    return EOK;
}

static void
mwl_setMcList(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    if (ic->ic_state == IEEE80211_S_RUN) {
        if (mwl_setFwMulticast(netdev, netdev->mc_list)) {
            MWL_DBG_WARN(DBG_CLASS_MAIN, "setting multicast addresses");
        }
    }
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
}

static int
mwl_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;
    int error = -EOPNOTSUPP;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    switch (cmd) {
        case SIOCETHTOOL:
            error = mwl_doEthtoolIoctl(ic, ifr, cmd);
            break;
        default:
            error = ieee80211_ioctl(ic, ifr, cmd);
            break;
    }
    MWL_DBG_EXIT_INFO(DBG_CLASS_MAIN, "result code is: %i", error);
    return error;
}

static struct net_device_stats *
mwl_getStats(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    if (netdev->flags & IFF_RUNNING) {
        mwl_getFwStatistics(netdev);
    }
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
    return &(mwlp->netDevStats);
}

static int
mwl_setMacAddr(struct net_device *netdev, void *addr)
{
    struct mwl_private *mwlp = netdev->priv;
    struct sockaddr *macAddr = (struct sockaddr *) addr;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    if (is_valid_ether_addr(macAddr->sa_data)) {
        if (IEEE80211_ADDR_EQ(macAddr->sa_data, mwlp->hwData.macAddr)) {
            MWL_DBG_EXIT(DBG_CLASS_MAIN);
            return EOK;
        }
        MWL_DBG_EXIT(DBG_CLASS_MAIN);
        return EOK; /* for safety do not allow changes in MAC-ADDR! */
        IEEE80211_ADDR_COPY(netdev->dev_addr, macAddr->sa_data);
        IEEE80211_ADDR_COPY(mwlp->hwData.macAddr, macAddr->sa_data);
        if (mwl_setFwMacAddress(netdev)) {
            MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting MAC address");
            return -EFAULT;
        }
        MWL_DBG_EXIT(DBG_CLASS_MAIN);
        return EOK;
    }
    MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "invalid addr");
    return -EADDRNOTAVAIL;
}

static int
mwl_changeMtu(struct net_device *netdev, int mtu)
{
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    if (ic->ic_state == IEEE80211_S_SCAN) {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "reject new MTU on scanning card");
        return -EAGAIN;
    }

    if ((mtu < MWL_MIN_MTU) || (mtu > MWL_MAX_MTU)) {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, 
            "invalid MTU size %d, min %u, max %u",mtu,MWL_MIN_MTU,MWL_MAX_MTU);
        return -EINVAL;
    }

    if (netdev->mtu != mtu) {
        MWL_DBG_INFO(DBG_CLASS_MAIN, "from MTU=%i to MTU=%i", netdev->mtu, mtu);
        if ((mtu >= IEEE80211_MTU_MIN) && (mtu <= IEEE80211_MTU_MAX)) {
            mwlp->isMtuChanged = MWL_TRUE;
            netdev->mtu = mtu;
            mwl_reset(netdev);
            mwlp->isMtuChanged = MWL_FALSE;
        }
    }
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
    return EOK;
}

/* 
** Reset the hardware w/o losing operational state. This is basically a
** more efficient way of doing stop, open, followed by state transitions
** to the current 802.11 operational state. Used to recover from errors rx
** overrun and to reset the hardware when rf gain settings must be reset
*/
static int
mwl_reset(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;
    struct ieee80211_node *ni = ic->ic_bss;
    mwl_facilitate_e radioOnOff = (ni->ni_txpower) ? MWL_ENABLE : MWL_DISABLE;
    u_int8_t macAddr[6] = { 0 };
    int rate;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    if (mwlp->inReset) {
        return EOK;
    } else {
        mwlp->inReset = MWL_TRUE;
    }

    if (ic->ic_state == IEEE80211_S_RUN) {
        netif_stop_queue(netdev);
        netdev->flags &= ~IFF_RUNNING;
    }

    if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
        if (mwl_setFwApBss(netdev, MWL_DISABLE)) {
            MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "disable AP bss");
            goto err_fw_cmd;
        }
    }

    if (mwl_setFwRadio(netdev, MWL_DISABLE, MWL_AUTO_PREAMBLE)) {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "disable radio");
        goto err_fw_cmd;
    }
    /* 
    ** free the finalized joined beacon information as well,
    ** so that it can be assigned again later... 
    */
    mwlp->usingGProtection = MWL_DISABLE;

    mwl_disableInterrupts(netdev);
    if (mwl_reloadFirmwareAndInitDescriptors(netdev)) {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "loading firmware");
        goto err_fw_cmd;
    }

    if (ic->ic_state == IEEE80211_S_RUN) {
        if (mwl_setFwAntenna(netdev, MWL_ANTENNATYPE_RX)) {
            MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting RX antenna");
            goto err_fw_cmd;
        }
        if (mwl_setFwAntenna(netdev, MWL_ANTENNATYPE_TX)) {
            MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting TX antenna");
            goto err_fw_cmd;
        }
        if (mwl_setFwRadio(netdev, radioOnOff, 
            (ic->ic_flags & IEEE80211_F_SHPREAMBLE) ? 
            MWL_SHORT_PREAMBLE : MWL_LONG_PREAMBLE)) {
            MWL_DBG_ERROR(DBG_CLASS_MAIN, "setting preamble");
            goto err_fw_cmd;
        }
        if (mwl_setFwTxPower(netdev, ni->ni_txpower)) {
            MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting TX power");
            goto err_fw_cmd;
        }
        if (mwl_setFwRTSThreshold(netdev, ic->ic_rtsthreshold)) {
            MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting RTS threshold");
            goto err_fw_cmd;
        }
        if (ic->ic_boost_mode != MWL_BOOST_MODE_REGULAR) {
            if (mwl_setFwBoostMode(netdev, ic->ic_boost_mode)) {
                MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting boost mode");
                goto err_fw_cmd;
            }
        } 
        if (ic->ic_opmode != IEEE80211_M_HOSTAP) {
            /* 
            ** The prescan and postscan cmds are mandatory required
            ** by FW, although meaningless from FSM point-of-view.
            ** If both FW cmds are not stated in this reset path,
            ** FW will not be able to send and receive any frame
            */
            if (mwl_setFwPrescan(netdev)) { 
                MWL_DBG_ERROR(DBG_CLASS_MAIN, "couldn't prescan");
                goto err_fw_cmd;
            }
            if (mwl_setFwPostscan(netdev, &macAddr[0], 1)) {
                MWL_DBG_ERROR(DBG_CLASS_MAIN, "couldn't postscan");
                goto err_fw_cmd;
            }
            if (mwl_channelSet(netdev, ic->ic_ibss_chan)) {
                MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting channel");
                goto err_fw_cmd;
            }
            if (mwl_setFwAid(netdev, ni->ni_bssid, ni->ni_associd)) {
                MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting AID");
                goto err_fw_cmd;
            }
            rate = mwl_rateIdxToRateMbps(mwlp->fixedRate);
            if (mwl_setFwRate(netdev, rate)) {
                MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting rate");
                goto err_fw_cmd;
            }
            if (mwl_setSlot(netdev)) {
                MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting slot");
                goto err_fw_cmd;
            }
            if (mwl_setFwMulticast(netdev, netdev->mc_list)) {
                MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting multicast");
                goto err_fw_cmd;
            }
        } else { /* IEEE80211_M_HOSTAP */
            if ((mwlp->fixedRate == 0) || (mwlp->fixedRate == -1)) {
                if (ic->ic_curmode == IEEE80211_MODE_11B) {
                    rate = mwl_rateMbpsToRateIdx(MWL_RATE_11_0MBPS);
                } else {
                    rate = mwl_rateMbpsToRateIdx(MWL_RATE_54_0MBPS);
                }
                ic->ic_fixed_rate = rate;
            }
            if (mwl_channelSet(netdev, ic->ic_ibss_chan)) {
                MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting channel");
                goto err_fw_cmd;
            }
            if (mwl_setFwRate(netdev, MWL_RATE_AUTOSELECT)) {
                MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting rate");
                goto err_fw_cmd;
            }
            if (mwl_setSlot(netdev)) {
                MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting slot");
                goto err_fw_cmd;
            }
            if (mwl_setFwApBeacon(netdev, ni)) {
                MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting AP beacon");
                goto err_fw_cmd;
            }
            if (mwl_setFwApBurstMode(netdev, MWL_DISABLE)) {
                MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "disabling AP burst");
                goto err_fw_cmd;
            }
            if (ic->ic_flags & IEEE80211_F_HIDESSID) {
                if (mwl_setFwApSsidBroadcast(netdev, MWL_DISABLE)) {
                    MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "disbl SSID broadcast");
                    goto err_fw_cmd;
                }
                if (mwl_setFwApWds(netdev, MWL_DISABLE)) {
                    MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "disabling AP wds");
                    goto err_fw_cmd;
                }
            } else {
                if (mwl_setFwApSsidBroadcast(netdev, MWL_ENABLE)) {
                    MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "enbl SSID broadcast");
                    goto err_fw_cmd;
                }
                if (mwl_setFwApWds(netdev, MWL_ENABLE)) {
                    MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "enabling AP wds");
                    goto err_fw_cmd;
                }
            }
            if (mwl_setFwApBss(netdev, radioOnOff)) {
                MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "enabling AP bss");
                goto err_fw_cmd;
            }
            if (mwl_setFwMulticast(netdev, netdev->mc_list)) {
                MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting multicast");
                goto err_fw_cmd;
            }
        }

        if (ic->ic_flags & IEEE80211_F_PRIVACY) {
            mwl_keyConfig(ic, MWL_SET);
        }
        netif_wake_queue(netdev);  /* restart Q if interface was running */
        netdev->flags |= IFF_RUNNING;
    }

    mwl_enableInterrupts(netdev);
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
    mwlp->inReset = MWL_FALSE;
    return EOK;

err_fw_cmd:
    mwlp->inReset = MWL_FALSE;
    return -EFAULT;
}
 
static void
mwl_txTimeout(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    if (mwlp->inReset) {
        return;
    }

    mwlp->isTxTimeout = MWL_TRUE;
    mwl_reset(netdev);
    mwlp->isTxTimeout = MWL_FALSE;
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
}

static int
mwl_reloadFirmwareAndInitDescriptors(struct net_device *netdev) 
{
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    mwl_resetAdapterAndVanishFw(netdev);

    writel(0x00000000, mwlp->ioBase0 + mwlp->descData.wcbBase);
    writel(0x00000000, mwlp->ioBase0 + mwlp->descData.rxDescRead);
    writel(0x00000000, mwlp->ioBase0 + mwlp->descData.rxDescWrite);

    mwl_cleanRxRing(netdev);
    mwl_freeRxRing(netdev);
    mwl_cleanTxRing(netdev);
    mwl_freeTxRing(netdev);

    if (mwl_downloadFirmware(netdev, ic->ic_opmode)) {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "download firmware");
        return MWL_DNLD_FWIMG_FAILED;
    }

    if (mwl_allocateTxRing(netdev) == 0) {
        if (mwl_initTxRing(netdev) != 0) {
            mwl_freeTxRing(netdev);
            MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "couldn't init TX");
            return MWL_ERROR;
        }
    } else {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "couldn't allocate TX");
        return MWL_ERROR;
    }

    if (mwl_allocateRxRing(netdev) == 0) {
        if (mwl_initRxRing(netdev) != 0) {
            mwl_freeRxRing(netdev);
            mwl_freeTxRing(netdev);
            MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "couldn't init RX");
            return MWL_ERROR;
        }
    } else {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "couldn't allocate RX");
        return MWL_ERROR;
    }

    if (mwl_getFwHardwareSpecs(netdev)) {
        mwl_cleanRxRing(netdev);
        mwl_freeRxRing(netdev);
        mwl_cleanTxRing(netdev);
        mwl_freeTxRing(netdev);
        MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "couldn't get HW specs");
        return MWL_ERROR;
    }

    writel((mwlp->descData.pPhysTxRing),
        mwlp->ioBase0 + mwlp->descData.wcbBase);
    writel((mwlp->descData.pPhysRxRing),
        mwlp->ioBase0 + mwlp->descData.rxDescRead);
    writel((mwlp->descData.pPhysRxRing),
        mwlp->ioBase0 + mwlp->descData.rxDescWrite);

    MWL_DBG_EXIT(DBG_CLASS_MAIN);
    return MWL_OK;
}

static void
mwl_nextScan(unsigned long arg)
{
    struct net_device *netdev = (struct net_device *) arg;
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    if (ic->ic_state == IEEE80211_S_SCAN) {
        if ((mwlp->isMtuChanged == MWL_TRUE) || 
            (mwlp->isTxTimeout  == MWL_TRUE)) {
            mod_timer(&mwlp->neighborScanTimer,jiffies + ((HZ * 3) / 10));
        } else {
            ieee80211_next_scan(ic);
        }
    }
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
}

static int
mwl_setupChannels(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;
    u_int16_t freq, flags;
    int currChannel, currEntry;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    for (currEntry = 0; currEntry < MAX_CHANNELMAP_ENTRIES; currEntry++) {
        if (mwlp->hwData.regionCode == channelMap[currEntry].regionCode) {
            for (currChannel = 0; 
                 currChannel < channelMap[currEntry].lenChannelMap; 
                 currChannel++) {
                if (ic->ic_channels[currChannel].ic_freq == 0) {
                    if (channelMap[currEntry].channelList[currChannel] != 0) {
                        /* need to check for the configured mode */
                        flags = IEEE80211_CHAN_PUREG | 
                                IEEE80211_CHAN_B | 
                                IEEE80211_CHAN_G;
                        freq = ieee80211_ieee2mhz(channelMap[currEntry]
                                  .channelList[currChannel],flags);
                        ic->ic_channels[currChannel].ic_freq = freq;
                        ic->ic_channels[currChannel].ic_flags = flags;
                    }
                } else {
                     /* channels overlap; e.g. 11g and 11b */
                    flags = IEEE80211_CHAN_PUREG | 
                            IEEE80211_CHAN_B | 
                            IEEE80211_CHAN_G;
                    ic->ic_channels[currChannel].ic_flags |= flags;
                }
            }
            MWL_DBG_EXIT(DBG_CLASS_MAIN);
            return MWL_OK;
        }
    }
    MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "couldn't populate channels");
    return MWL_ERROR;
}

static int
mwl_setupRates(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;
    int currRate;
    int numRates;
    
    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    numRates = sizeof(ratesBmode)/sizeof(u_int8_t);
    for (currRate = 0; currRate < numRates; currRate++) {
        ic->ic_sup_rates[IEEE80211_MODE_11B].rs_rates[currRate] = 
            ratesBmode[currRate]; /* .dot11Rate; */
    }
    ic->ic_sup_rates[IEEE80211_MODE_11B].rs_nrates = numRates;

    numRates = sizeof(ratesGmode)/sizeof(u_int8_t);
    for (currRate = 0; currRate < numRates; currRate++) {
        ic->ic_sup_rates[IEEE80211_MODE_11G].rs_rates[currRate] = 
            ratesGmode[currRate]; /* .dot11Rate; */
    }
    ic->ic_sup_rates[IEEE80211_MODE_11G].rs_nrates = numRates;

    MWL_DBG_EXIT(DBG_CLASS_MAIN);
    return MWL_OK;
}

/*******************************************************************************
 * note that this function returns the converted rate as a 
 * multiple of 0.5 Mbps. Thus, a data rate of 36Mbps will
 * result in a return value of 72 (0.5Mbps * 72 = 36Mbps)
 ******************************************************************************/
static int
mwl_rateIdxToRateMbps(int rateIdx)
{
    int rateMbps = MWL_RATE_AUTOSELECT; /* default setting */

    MWL_DBG_ENTER_INFO(DBG_CLASS_MAIN, "passed rateIdx is %i", rateIdx);

    if (rateIdx != -1) {
        rateMbps = ratesGmode[rateIdx]; /* includes also B mode rates! */
        rateMbps &= 0x007f;
    } 

    MWL_DBG_EXIT_INFO(DBG_CLASS_MAIN, "return rateMbps of %i", rateMbps);
    return rateMbps;
}

/*******************************************************************************
 * note that this function requires the rate mbps to be
 * passes as multiples of 0.5 Mbps. Thus, a data rate of 
 * 36Mbps needs to be stataed as a value of 72 (0.5Mbps * 72 = 36Mbps)
 ******************************************************************************/
static int
mwl_rateMbpsToRateIdx(mwl_rate_e rateMbps)
{
    int rateIdx = 0; /* default setting */
    int currRateIndex;

    MWL_DBG_ENTER_INFO(DBG_CLASS_MAIN, "passed rateMpbs is %i", rateMbps);

    for (currRateIndex = 0;currRateIndex < sizeof(ratesGmode);currRateIndex++){
        if ((ratesGmode[currRateIndex] & 0x7f) == (rateMbps & 0x7f)) {
           rateIdx = currRateIndex;
        }
    }

    MWL_DBG_EXIT_INFO(DBG_CLASS_MAIN, "return rateIdx of %i", rateIdx);
    return rateIdx;
}

static int
mwl_channelSet(struct net_device *netdev, struct ieee80211_channel *channel)
{
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;
    // struct ieee80211_node *ni = ic->ic_bss;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    if ((channel->ic_freq  != mwlp->currChannel.ic_freq) ||
        (channel->ic_flags != mwlp->currChannel.ic_flags)) {
        mwlp->currChannel = *channel;
        ic->ic_ibss_chan = channel; 
    }
    if (mwl_setFwChannel(netdev, ieee80211_chan2ieee(ic, channel))) {
        MWL_DBG_WARN(DBG_CLASS_MAIN, "channel set failed");
    }
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
    return MWL_OK;
}

static struct ieee80211_node *
mwl_nodeAlloc(struct ieee80211com *ic)
{
    unsigned int length = sizeof(struct mwl_node);
    struct mwl_node *mwln = kmalloc(length, GFP_ATOMIC);

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    if (mwln != NULL) {
        memset(mwln, 0x00, length);
        MWL_DBG_EXIT(DBG_CLASS_MAIN);
        return &mwln->node;
    }
    MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "no mem");
    return NULL;
}

static void
mwl_nodeCopy(struct ieee80211com *ic, struct ieee80211_node *dst, const struct ieee80211_node *src)
{
    struct net_device *netdev = ic->ic_dev;
    struct mwl_private *mwlp = netdev->priv;
    struct mwl_node *mwln = (struct mwl_node *) dst;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    /* 
    ** NB: Must copy first so the cleanup done by node_copy is
    **     done before we copy bits around below. 
    */
    mwlp->parentFuncs.mwl_nodeCopy(ic, dst, src);
    memcpy(&dst[1], &src[1],
           sizeof(struct mwl_node) - sizeof(struct ieee80211_node));
    mwln->beaconData.pSkBuff = NULL;
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
}

static void
mwl_nodeCleanup(struct ieee80211com *ic, struct ieee80211_node *ni)
{
    struct net_device *netdev = ic->ic_dev;
    struct mwl_private *mwlp = netdev->priv;
    struct mwl_node *mwln = (struct mwl_node *) ni;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    if (mwln->beaconData.pSkBuff != NULL) {
        dev_kfree_skb_any(mwln->beaconData.pSkBuff);
        mwln->beaconData.pSkBuff = NULL;
        MWL_DBG_INFO(DBG_CLASS_MAIN, "free'd allocated beacon buff");
    }

    mwlp->parentFuncs.mwl_nodeCleanup(ic, ni);
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
}

static u_int8_t
mwl_nodeGetRssi(struct ieee80211com *ic, struct ieee80211_node *ni)
{
    struct mwl_node *mwln = (struct mwl_node *) ni;
    return mwln->rssi;
}

static int
mwl_newState(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
    struct net_device *netdev = ic->ic_dev;
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211_node *ni = ic->ic_bss;
    mwl_facilitate_e radioOnOff = (ni->ni_txpower) ? MWL_ENABLE : MWL_DISABLE;
    u_int8_t macAddr[6] = { 0 };
    u_int8_t lastChannel = 1;
    u_int8_t *bssId;
    int retVal = 0;
    int rate;

    MWL_DBG_ENTER_INFO(DBG_CLASS_MAIN, "%s -> %s (phy mode: %i)", 
        ieee80211_state_name[ic->ic_state],
        ieee80211_state_name[nstate], ic->ic_curmode);

    del_timer(&mwlp->neighborScanTimer);  /* kill if running */
    netif_stop_queue(netdev); /* before we do anything else */

    if ((mwlp->isMtuChanged == MWL_TRUE) || 
        (mwlp->isTxTimeout  == MWL_TRUE)) {
        MWL_DBG_WARN(DBG_CLASS_MAIN, "newState while operation pending");
        goto starttimer;
    }

    /*
    ** Avoid any TX timeout detection by the Linux kernel!
    ** 
    ** If the driver is running in station mode and is not
    ** able to find any suitable AP, it will proceeed with
    ** a passive scan until a suitable AP is detected.
    **
    ** This is fine, but Linux has an interface watchdog 
    ** running which checks for the tx queue and the timestamp
    ** of the last packet sent. If the queue is not enabled
    ** and not packets have been sent, Linux will trigger 
    ** a netdev->tx_timeout :-(, which forces a reload of FW
    ** what may interfere with the passive scan ongoing...
    */
    if (nstate == IEEE80211_S_SCAN) {
        if (ic->ic_state == IEEE80211_S_SCAN) {
            if (ieee80211_chan2ieee(ic, ni->ni_chan) == lastChannel) {
                MWL_DBG_INFO(DBG_CLASS_MAIN, "fake TX time");
                netdev->trans_start = jiffies; /* fake send */
            }
        } else {
            lastChannel = ieee80211_chan2ieee(ic, ni->ni_chan);
        }
    }

    if (nstate == IEEE80211_S_INIT) {
        goto done;
    } else if (nstate == IEEE80211_S_SCAN) {
        bssId = netdev->broadcast;
        if ((ic->ic_state == IEEE80211_S_INIT) || 
            (ic->ic_state == IEEE80211_S_AUTH)) {
            if (ic->ic_opmode != IEEE80211_M_IBSS) {
                if (mwl_setFwPrescan(netdev)) {
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "couldn't prescan");
                    goto starttimer;
                }
            }
            if (ic->ic_state == IEEE80211_S_AUTH) {
                ic->ic_fixed_rate = mwlp->fixedRate; /* restore init value */
            }
        }
        if (mwl_channelSet(netdev, ni->ni_chan)) {
            MWL_DBG_ERROR(DBG_CLASS_MAIN, "couldn't set channel");
            goto starttimer;
        }
    } else if (nstate == IEEE80211_S_AUTH) {
        if (ic->ic_state == IEEE80211_S_SCAN) {
            if (ic->ic_opmode == IEEE80211_M_STA) {
                /*
                ** Note that mwl_setFwPostscan() ends the scan and sets the 
                ** RF channel of the adapter back to the channel that was 
                ** configured when triggering mwl_setFwPrescan()! Therefore 
                ** we have to effectively set the last channel again (just
                ** to be sure the FW has correct channel set).
                */
                if (mwl_setFwPostscan(netdev, &macAddr[0], 1)) {
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "couldn't postscan");
                    goto bad;
                }
                if (mwl_channelSet(netdev, ni->ni_chan)) {
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "couldn't set channel");
                    goto starttimer;
                }
            }
        }
    } else if (nstate == IEEE80211_S_ASSOC) {
        /* tbd */
    } else if (nstate == IEEE80211_S_RUN) {
        bssId = ni->ni_bssid;
#if 0
        MWL_DBG_INFO("ic_flags=0x%08x iv=%d bssid=%s capinfo=0x%04x chan=%d\n",
            netdev->name, ic->ic_flags, ni->ni_intval, 
            ether_sprintf(ni->ni_bssid), ni->ni_capinfo, 
            ieee80211_chan2ieee(ic, ni->ni_chan));
#endif
        if (ic->ic_state == IEEE80211_S_INIT) {
            /* tbd */
        } else if (ic->ic_state == IEEE80211_S_SCAN) {
            if (ic->ic_opmode == IEEE80211_M_STA) {
                /* tbd */
            } else if (ic->ic_opmode == IEEE80211_M_IBSS) {
                if (mwl_beaconAlloc(netdev, ni, MWL_TRUE)) {
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "couldn't alloc beacon");
                    goto bad; /* Config beacon and sleep timers */
                }
                if (mwl_setFwStationBeacon(netdev, ni, 1)) {
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "setting FW beacon");
                    goto bad; /* unfortunately not successful :-( */
                }
                if (mwl_setFwFinalizeJoin(netdev, ni, 0)) {
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "finalize join");
                    goto bad; /* bad luck :-( */
                }
            } else if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
                if ((ic->ic_fixed_rate == 0) || (ic->ic_fixed_rate == -1)) {
                    if (ic->ic_curmode == IEEE80211_MODE_11B) {
                        rate = mwl_rateMbpsToRateIdx(MWL_RATE_11_0MBPS);
                    } else {
                        rate = mwl_rateMbpsToRateIdx(MWL_RATE_54_0MBPS);
                    }
                    mwlp->fixedRate = ic->ic_fixed_rate; /* save value */
                    ic->ic_fixed_rate = rate; /* for the display */
                }
                if (ic->ic_curmode == IEEE80211_MODE_11B) {
                    ic->ic_fixed_rate=mwl_rateMbpsToRateIdx(MWL_RATE_11_0MBPS);
                } else {
                    ic->ic_fixed_rate=mwl_rateMbpsToRateIdx(MWL_RATE_54_0MBPS);
                }
                if (mwl_beaconAlloc(netdev, ni, MWL_FALSE)) {
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "alloc beacon");
                    goto bad; /* Config beacon and sleep timers */
                }
                if (mwl_setFwPostscan(netdev, &macAddr[0], 1)) {
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "couldn't postscan");
                    goto bad;
                }
                if (mwl_setFwRate(netdev, MWL_RATE_AUTOSELECT)) {
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "setting rate");
                    goto bad;
                }
                if (mwl_setFwApBss(netdev, MWL_DISABLE)) {
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "disabling AP bss");
                    goto bad;
                }
                if (mwl_setSlot(netdev)) { 
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "setting slot");
                    goto bad;
                }
                if (mwl_setFwApBeacon(netdev, ni)) {
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "setting AP beacon");
                    goto bad;
                }
                if (mwl_setFwApBurstMode(netdev, MWL_DISABLE)) {
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "disabling AP burst");
                    goto bad;
                }
                if (ic->ic_flags & IEEE80211_F_HIDESSID) {
                    if (mwl_setFwApSsidBroadcast(netdev, MWL_DISABLE)) {
                        MWL_DBG_ERROR(DBG_CLASS_MAIN, "disabling AP SSID");
                        goto bad;
                    }
                    if (mwl_setFwApWds(netdev, MWL_DISABLE)) {
                        MWL_DBG_ERROR(DBG_CLASS_MAIN, "disabling AP wds");
                        goto bad;
                    }
                } else {
                    if (mwl_setFwApSsidBroadcast(netdev, MWL_ENABLE)) {
                        MWL_DBG_ERROR(DBG_CLASS_MAIN, "enabling AP SSID");
                        goto bad;
                    }
                    if (mwl_setFwApWds(netdev, MWL_ENABLE)) {
                        MWL_DBG_ERROR(DBG_CLASS_MAIN, "enabling AP wds");
                        goto bad;
                    }
                }
                if (mwl_setFwApBss(netdev, MWL_ENABLE)) {
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "enabling AP bss");
                    goto bad;
                }
                if (mwl_setFwMulticast(netdev, netdev->mc_list)) {
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "setting MC list");
                    goto bad;
                }
            } else if (ic->ic_opmode == IEEE80211_M_MONITOR) {
                /* tbd */
            } else {
                printk(KERN_ERR "%s: opmode? (%i=0x%x)\n",
                        netdev->name, ic->ic_opmode, ic->ic_opmode);
            }
        } else if (ic->ic_state == IEEE80211_S_AUTH) {
            /* tbd */
        } else if (ic->ic_state == IEEE80211_S_ASSOC) {
            if (ic->ic_opmode == IEEE80211_M_STA) {
                if (mwl_setFwRadio(netdev, radioOnOff, 
                        (ic->ic_flags & IEEE80211_F_SHPREAMBLE) ? 
                        MWL_SHORT_PREAMBLE : MWL_LONG_PREAMBLE)) {
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "setting preamble");
                    goto bad;
                }
                if (mwl_setFwAid(netdev, bssId, ni->ni_associd)) {
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "couldn't set AID");
                    goto bad;
                }
                mwlp->fixedRate = ic->ic_fixed_rate; /* save value */
                rate = mwl_rateIdxToRateMbps(ic->ic_fixed_rate);
                if (mwl_setFwRate(netdev, rate)) {
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "couldn't set rate");
                    goto bad;
                }
                if (mwl_setSlot(netdev)) { 
                    MWL_DBG_ERROR(DBG_CLASS_MAIN, "couldn't set slot");
                    goto bad;
                }
            }
        } else if (ic->ic_state == IEEE80211_S_RUN) {
            printk(KERN_INFO "%s: transition RUN -> RUN ???\n", netdev->name);
        } else {
            printk(KERN_ERR "%s: state ? (0x%x)\n",netdev->name, ic->ic_state);
        }
        netif_start_queue(netdev);
    } else {
        printk(KERN_ERR "%s: newstate ? (0x%x)\n", netdev->name, nstate);
    } /* IEEE80211_S_RUN */

    if (ic->ic_flags & IEEE80211_F_PRIVACY) {
        if ((nstate == IEEE80211_S_RUN) && (ic->ic_state != IEEE80211_S_RUN)) {
            mwl_keyConfig(ic, MWL_SET);
        }
        if ((nstate != IEEE80211_S_RUN) && (ic->ic_state == IEEE80211_S_RUN)) {
            mwl_keyConfig(ic, MWL_RESET);
        }
    }

done:
    retVal = mwlp->parentFuncs.mwl_newState(ic, nstate, arg);
starttimer:
    if (nstate == IEEE80211_S_SCAN) {
        mod_timer(&mwlp->neighborScanTimer,jiffies + ((HZ * 3) / 10));
    }
bad:
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
    return retVal;
}

static int
mwl_mediaChange(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;
    int rate = ic->ic_fixed_rate; /* save before media_change() */
    int retVal = ieee80211_media_change(netdev);

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    if (rate != ic->ic_fixed_rate) { 
        mwlp->fixedRate = ic->ic_fixed_rate;
    }

    if (retVal == ENETRESET) {
        if ((netdev->flags & (IFF_RUNNING|IFF_UP)) == (IFF_RUNNING|IFF_UP)) {
            retVal = mwl_reset(netdev);
        } else {
            retVal = IEEE80211_ERROR;
        }
    }
    MWL_DBG_EXIT_INFO(DBG_CLASS_MAIN, "return value is %i", retVal);
    return retVal;
}

static void
mwl_keyConfig(struct ieee80211com *ic, mwl_operation_e action)
{
    struct ieee80211_key *k = NULL;
    u_int16_t currKey;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    mwl_keyUpdateBegin(ic);
    for (currKey = 0; currKey < IEEE80211_WEP_NKID; currKey++) {
        k = &ic->ic_nw_keys[currKey];
	if (k->wk_cipher->ic_cipher == IEEE80211_CIPHER_WEP) {
            if (action == MWL_SET) {
                mwl_keySet(ic, k, ic->ic_myaddr);
            } else {
                mwl_keyDelete(ic, k);
            }
        }
    }
    mwl_keyUpdateEnd(ic);

    MWL_DBG_EXIT(DBG_CLASS_MAIN);
}

static void
mwl_keyUpdateBegin(struct ieee80211com *ic)
{
    struct net_device *netdev = ic->ic_dev;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);
    netif_stop_queue(netdev); /* block processing while key change */
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
}

static int
mwl_keyAlloc(struct ieee80211com *ic, const struct ieee80211_key *key)
{
    MWL_DBG_ENTER_INFO(DBG_CLASS_MAIN, "alloc key %s (idx=%i, flags=0x%x)",
        key->wk_cipher->ic_name, key->wk_keyix, key->wk_flags);

    MWL_DBG_EXIT(DBG_CLASS_MAIN);
    return 0; /* first index is returned */
}

static int
mwl_keyDelete(struct ieee80211com *ic, const struct ieee80211_key *key)
{
    struct net_device *netdev = ic->ic_dev;
    int retval = IEEE80211_OK; /* initial default */
    u_int8_t mac[IEEE80211_ADDR_LEN];

    MWL_DBG_ENTER_INFO(DBG_CLASS_MAIN, "del key %s (idx=%i, flags=0x%x)",
        key->wk_cipher->ic_name, key->wk_keyix, key->wk_flags);

    if (key->wk_cipher->ic_cipher != IEEE80211_CIPHER_NONE) {
        if (key->wk_cipher->ic_cipher == IEEE80211_CIPHER_WEP) {
            retval = mwl_setFwWepKey(netdev, key, MWL_RESET);
        } else {
            if (key->wk_keyix != 0) {
                retval = mwl_setFwGroupKey(netdev, key, MWL_RESET);
            } else {
                retval = mwl_setFwPairwiseKey(netdev, key, mac, MWL_RESET);
            }
        }
    }

    MWL_DBG_EXIT(DBG_CLASS_MAIN);
    return (retval) ? 0 : IEEE80211_OK;
}

static int
mwl_keySet(struct ieee80211com *ic, const struct ieee80211_key *key, const u_int8_t mac[IEEE80211_ADDR_LEN])
{
    struct net_device *netdev = ic->ic_dev;
    int retval = 0; /* initial default */

    MWL_DBG_ENTER_INFO(DBG_CLASS_MAIN, "set key %s (idx=%i, flags=0x%x)",
        key->wk_cipher->ic_name, key->wk_keyix, key->wk_flags);

    if (key->wk_cipher->ic_cipher != IEEE80211_CIPHER_NONE) {
        if (key->wk_cipher->ic_cipher == IEEE80211_CIPHER_WEP) {
            retval = mwl_setFwWepKey(netdev, key, MWL_SET);
        } else {
            if (key->wk_keyix != 0) {
                retval = mwl_setFwGroupKey(netdev, key, MWL_SET);
            } else {
                retval = mwl_setFwPairwiseKey(netdev, key, mac, MWL_SET);
            }
        }
    }

    MWL_DBG_EXIT(DBG_CLASS_MAIN);
    return (retval) ? 0 : IEEE80211_OK;
}

static void
mwl_keyUpdateEnd(struct ieee80211com *ic)
{
    struct net_device *netdev = ic->ic_dev;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);
    netif_start_queue(netdev); /* allow processing after key change */
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
}

static void
mwl_newAssoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{
    // struct net_device *netdev = ic->ic_dev;
    // struct mwl_private *mwlp = netdev->priv;
    MWL_DBG_INFO(DBG_CLASS_MAIN, "association (%s)", (isnew)? "new":"old");
}

/*
** Set the slot time based on the current setting.
*/
static int
mwl_setSlot(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    if (ic->ic_opmode != mwlp->initialOpmode) {
        MWL_DBG_EXIT_INFO(DBG_CLASS_MAIN, "mode-FW mismatch");
        return MWL_OK;
    }

    if (ic->ic_flags & IEEE80211_F_SHSLOT) {
        if (mwl_setFwSlot(netdev, MWL_SHORTSLOT)) { 
            MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting short slot");
            return MWL_ERROR;
        }
    } else {
        if (mwl_setFwSlot(netdev, MWL_LONGSLOT)) { 
            MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setting regular slot");
            return MWL_ERROR;
        }
    }
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
    return MWL_OK;
}

/*
** Callback from the 802.11 layer to update slot time
** needs also an update in the beacon!
*/
static void
mwl_updateSlot(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;
    struct ieee80211_node *ni = ic->ic_bss;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    if (mwl_setSlot(netdev)) { 
        MWL_DBG_ERROR(DBG_CLASS_MAIN, "setting slot");
    }
    if (ic->ic_state == IEEE80211_S_RUN) {
        if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
            if (mwl_setFwApBss(netdev, MWL_DISABLE)) {
                MWL_DBG_ERROR(DBG_CLASS_MAIN, "disabling AP bss");
            }
            if (mwl_setFwApBeacon(netdev, ni)) {
                MWL_DBG_ERROR(DBG_CLASS_MAIN, "setting AP beacon");
            }
            if (mwl_setFwApBss(netdev, MWL_ENABLE)) {
                MWL_DBG_ERROR(DBG_CLASS_MAIN, "enabling AP bss");
            }
        }
    }
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
}

/*
** Allocate and setup an initial beacon frame.
*/
static int
mwl_beaconAlloc(struct net_device *netdev, struct ieee80211_node *ni, mwl_bool_e doPrependFwHeader)
{
    struct mwl_private *mwlp = netdev->priv;
    struct mwl_node *mwln = (struct mwl_node *) ni;
    struct ieee80211com *ic = &mwlp->curr80211com;
    struct sk_buff *newskb;
    struct sk_buff *skb;
    struct timeval tv;
    u_int64_t timeStamp;

    MWL_DBG_ENTER_INFO(DBG_CLASS_MAIN, "prepend additional FW info: %s",
        (doPrependFwHeader == MWL_TRUE) ? "yes": "no");

    if (mwln->beaconData.pSkBuff != NULL) {
        dev_kfree_skb_any(mwln->beaconData.pSkBuff);
        mwln->beaconData.pSkBuff = NULL;
        MWL_DBG_INFO(DBG_CLASS_MAIN, "free'd allocated beacon buff");
    }

    skb = ieee80211_beacon_alloc(ic,ni,&mwln->beaconData.beaconOffsets);
    if (skb== NULL) {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "failed to get beacon skbuff");
        return MWL_ERROR;
    }
#if 0
    if (mwl_beaconSetup(netdev)) {
        dev_kfree_skb_any(skb);
        MWL_DBG_EXIT_ERROR(DBG_CLASS_MAIN, "setup beacon failed");
        return MWL_ERROR;
    }
#endif

    /* offset 24 is the timestamp... */
    do_gettimeofday(&tv);
    timeStamp = tv.tv_usec + tv.tv_sec*1000000 + ni->ni_timoff;
    memcpy(&skb->data[24], &timeStamp, sizeof(u_int64_t));

    if (doPrependFwHeader) {
        /*
        ** prepend the two byte length indication for CB35 firmware
        ** plus the 6 bytes ADDR4 element!
        */
        newskb = alloc_skb((skb->len + NBR_BYTES_ADD_TXFWINFO), GFP_ATOMIC);
        memset(newskb->data, 0x00, (skb->len + NBR_BYTES_ADD_TXFWINFO));

        memcpy(&newskb->data[OFFS_TXFWBUFF_IEEE80211HEADER],
               &skb->data[OFFS_IEEE80211HEADER],
               NBR_BYTES_IEEE80211HEADER);

        memcpy(&newskb->data[OFFS_TXFWBUFF_IEEE80211PAYLOAD],
               &skb->data[NBR_BYTES_IEEE80211HEADER],
               skb->len - NBR_BYTES_IEEE80211COPYLEN);

        newskb->len = skb->len + NBR_BYTES_ADD_TXFWINFO;
        newskb->data[0] = newskb->len; // len is full len incl ADDR4 + FWLEN
        dev_kfree_skb_any(skb); /* get rid of old skb */
        mwln->beaconData.pSkBuff = newskb;
        MWL_DBG_INFO(DBG_CLASS_MAIN, "free'd initial beacon buff");
    } else {
        mwln->beaconData.pSkBuff = skb;
    }
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
    return MWL_OK;
}

/*
** Setup the beacon frame. 
*/
static int
mwl_beaconSetup(struct net_device *netdev)
{
    // struct mwl_private *mwlp = netdev->priv;
    // struct ieee80211com *ic = &mwlp->curr80211com;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    /*
    ** need to map the buffer to the HW can maybe done with a normal xmit?
    ** mwlp->beaconData contains all data...
    */
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
    return MWL_OK;
}

/*
** Configure the beacon and sleep timers.
**
** When operating as an AP this resets the TSF and sets
** up the hardware to notify us when we need to issue beacons.
**
** When operating in station mode this sets up the beacon timers according
** to the timestamp of the last received beacon and the current TSF, 
** configures PCF and DTIM handling, programs the sleep registers so the
** hardware will wakeup in time to receive beacons, and configures the 
** beacon miss handling so we'll receive a BMISS interrupt when we stop 
** seeing beacons from the AP we've associated with.
** 
** NEED TO VERIFIY REGARDING CB35P!!
*/
static void
mwl_beaconConfig(struct net_device *netdev)
{
#if 0
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;
    struct ieee80211_node *ni = ic->ic_bss; 
    u_int32_t nexttbtt, intval;

    MWL_DBG_ENTER(DBG_CLASS_MAIN);

    nexttbtt = (LE_READ_4(ni->ni_tstamp.data + 4) << 22) |
         (LE_READ_4(ni->ni_tstamp.data) >> 10);
    nexttbtt += ni->ni_intval;
    intval = ni->ni_intval & MWL_BEACON_PERIOD;

    if (ic->ic_opmode == IEEE80211_M_STA) {
        MWL_BEACON_STATE bs;
        u_int32_t bmisstime;

        /* NB: no PCF support right now */
        memset(&bs, 0, sizeof(bs));
        /* Reset our tsf so the hardware will update the */
        /* tsf register to reflect timestamps found in */
        /* received beacons. */
        bs.bs_intval = intval | MWL_BEACON_RESET_TSF;
        bs.bs_nexttbtt = nexttbtt;
        bs.bs_dtimperiod = bs.bs_intval;
        bs.bs_nextdtim = nexttbtt;
        /* The 802.11 layer records the offset to the DTIM */
        /* bitmap while receiving beacons; use it here to */
        /* enable h/w detection of our AID being marked in */
        /* the bitmap vector (to indicate frames for us are */
        /* pending at the AP). */
        bs.bs_timoffset = ni->ni_timoff;
        /* Calculate the number of consecutive beacons to miss */
        /* before taking a BMISS interrupt.  The configuration */
        /* is specified in ms, so we need to convert that to */
        /* TU's and then calculate based on the beacon interval. */
        /* Note that we clamp the result to at most 10 beacons. */
        bmisstime = (ic->ic_bmisstimeout * 1000) / 1024;
        bs.bs_bmissthreshold = howmany(bmisstime,ni->ni_intval);
        if (bs.bs_bmissthreshold > 10)
             bs.bs_bmissthreshold = 10;
        else if (bs.bs_bmissthreshold <= 0)
             bs.bs_bmissthreshold = 1;

        /* Calculate sleep duration.  The configuration is */
        /* given in ms.  We insure a multiple of the beacon */
        /* period is used.  Also, if the sleep duration is */
        /* greater than the DTIM period then it makes senses */
        /* to make it a multiple of that. */
        /* XXX fixed at 100ms */
        bs.bs_sleepduration =
             roundup((100 * 1000) / 1024, bs.bs_intval);

        if (bs.bs_sleepduration > bs.bs_dtimperiod) {
             // bs.bs_sleepduration = 
             //     roundup(bs.bs_sleepduration, bs.bs_dtimperiod);
        }
        mwl_disableInterrupts(netdev);
        // ath_hal_beacontimers(ah, &bs);
        // mwlp->sc_imask |= MWL_INT_BMISS;
        mwl_enableInterrupts(netdev);
    } else {
        mwl_disableInterrupts(netdev);
        if (nexttbtt == ni->ni_intval)
            // intval |= MWL_BEACON_RESET_TSF;
        if (ic->ic_opmode != IEEE80211_M_MONITOR) {
            // intval |= MWL_BEACON_ENA;
            // sc->sc_imask |= MWL_INT_SWBA;   /* beacon prepare */
        }
        mwl_enableInterrupts(netdev);
    }
    MWL_DBG_EXIT(DBG_CLASS_MAIN);
#endif
}

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
