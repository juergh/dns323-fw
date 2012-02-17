/*******************************************************************************
 *
 * Name:        mwl_main.h
 * Project:     Linux SoftAP for Marvell CB32/CB35 cardbus adapter
 * Version:     $Revision: 1.1.1.1 $
 * Date:        $Date: 2009/08/19 08:02:21 $
 * Author:      Ralph Roesler (rroesler@syskonnect.de)
 * Purpose:     main header file
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

#ifndef MWL_MAIN_H
#define MWL_MAIN_H

#include <linux/version.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/if_ether.h>   
#include <linux/if_arp.h>   
#include <linux/init.h> /* For __init, __exit */

#include "mwl_descriptors.h"
#include "mwl_wlanext.h"
#include <ieee80211_proto.h>

/*******************************************************************************
 *
 * Generic driver defines
 *
 ******************************************************************************/

#ifdef MWL_KERNEL_24
#undef IRQ_NONE 
#define IRQ_NONE 0
#undef IRQ_HANDLED
#define IRQ_HANDLED 1
#ifndef irqreturn_t
#define irqreturn_t void
#endif
#endif 

extern int mwl_init(struct net_device *, u_int16_t);
extern int mwl_deinit(struct net_device *);
extern irqreturn_t mwl_isr(int, void *, struct pt_regs *);
extern void mwl_enableInterrupts(struct net_device *);
extern void mwl_disableInterrupts(struct net_device *);
extern void mwl_resetAdapterAndVanishFw(struct net_device *);
extern int mwl_isAdapterPluggedIn(struct net_device *);

typedef enum {
    MWL_FALSE = 0,
    MWL_TRUE = 1,
} mwl_bool_e;

typedef enum {
    MWL_OK = 0,
    MWL_ERROR = 1,
    MWL_TIMEOUT = 2,
    MWL_DNLD_HLPER_FAILED = 3,
    MWL_DNLD_FWIMG_FAILED = 4,
} mwl_result_e;

typedef enum {
    MWL_ADAPTER_NOT_PLUGGED = 0,
    MWL_ADAPTER_PLUGGED_IN = 1,
} mwl_adapter_plug_state_e;

#define MAX_NUM_TX_DESC        256
#define MAX_NUM_RX_DESC        256
#define MIN_BYTES_HEADROOM      64
#define NUM_EXTRA_RX_BYTES     (2*MIN_BYTES_HEADROOM)

#define ISR_SRC_BITS        ((MACREG_A2HRIC_BIT_RX_RDY)   | \
                             (MACREG_A2HRIC_BIT_TX_DONE)  | \
                             (MACREG_A2HRIC_BIT_OPC_DONE) | \
                             (MACREG_A2HRIC_BIT_MAC_EVENT))

#define ENDIAN_SWAP32(_val)   (cpu_to_le32(_val))
#define ENDIAN_SWAP16(_val)   (cpu_to_le16(_val))

static const struct {
    u_int8_t        regionCode;           /* region code returned by FW       */
    char            countryString[3];     /* three ASCII ciphers of domain    */
    u_int8_t        firstChannelNbr;      /* first channel of the full list   */
    u_int8_t        numberChannels;       /* full number of channels in list  */
    int8_t          maxTxPowerLevel;      /* signed number expressed in dBms  */
    u_int8_t        lenChannelMap;        /* lenght of channelMap below       */
    unsigned int    channelList[15];      /* plain list of channels of domain */
} channelMap[] = {
    { 0x10,"FCC", 1,11,20,12, { 0,1,2,3,4,5,6,7,8,9,10,11          } },
    { 0x20," IC", 1,11,20,12, { 0,1,2,3,4,5,6,7,8,9,10,11          } },
    { 0x30," CE", 1,13,20,14, { 0,1,2,3,4,5,6,7,8,9,10,11,12,13    } },
    { 0x31,"SPA",10, 2,20,12, { 0,0,0,0,0,0,0,0,0,0,10,11          } },
    { 0x32,"FRA",10, 4,20,12, { 0,0,0,0,0,0,0,0,0,0,10,11,12,13    } },
    { 0x40,"JAP", 1,14,20,15, { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14 } },
    { 0x80,"DGT", 1,11,20,15, { 0,1,2,3,4,5,6,7,8,9,10,11          } },
    { 0x81,"AUS", 1,13,20,14, { 0,1,2,3,4,5,6,7,8,9,10,11,12,13    } }
};
#define MAX_CHANNELMAP_ENTRIES ((sizeof(channelMap))/(sizeof(channelMap[0])))

struct mwl_beacon_data {
    struct sk_buff                  *pSkBuff;       /* allocated BEACON frame */
    dma_addr_t                       pPhysAddr;     /* ptr to addr (physical) */
    struct ieee80211_node           *beaconNode;
    struct ieee80211_beacon_offsets  beaconOffsets;
};

struct mwl_node {
    struct ieee80211_node  node;           /* IEEE 802.11 base node           */
    u_int32_t              rssi;           /* current receive rssi (average)  */
    u_int32_t              sumRssi;        /* cumulated rssi value            */
    u_int32_t              numRssiValues;  /* num evaluates rssi values       */
    u_int8_t               lastRxRate;     /* last rx rate                    */
    mwl_bool_e             lastRxRateWasRateOfBmode; /* for tx rate in B mode */
    struct mwl_beacon_data beaconData;     /* possibly received beacon        */
};

struct mwl_desc_data {
    dma_addr_t       pPhysTxRing;          /* ptr to first TX desc (phys.)    */
    mwl_txdesc_t    *pTxRing;              /* ptr to first TX desc (virt.)    */
    mwl_txdesc_t    *pNextTxDesc;          /* next TX desc that can be used   */
    mwl_txdesc_t    *pStaleTxDesc;         /* the staled TX descriptor        */
    dma_addr_t       pPhysRxRing;          /* ptr to first RX desc (phys.)    */
    mwl_rxdesc_t    *pRxRing;              /* ptr to first RX desc (virt.)    */
    mwl_rxdesc_t    *pNextRxDesc;          /* next RX desc that can be used   */
    unsigned int     wcbBase;              /* FW base offset for registers    */
    unsigned int     rxDescWrite;          /* FW descriptor write position    */
    unsigned int     rxDescRead;           /* FW descriptor read position     */
    unsigned int     rxBufSize;            /* length of the RX buffers        */
};

struct mwl_locks {   
    spinlock_t       xmitLock;             /* used to protect TX actions      */
    spinlock_t       fwLock;               /* used to protect FW commands     */
};

struct mwl_hw_data {
    u_int32_t        fwReleaseNumber;      /* MajNbr:MinNbr:SubMin:PatchLevel */
    u_int8_t         hwVersion;            /* plain number indicating version */
    u_int8_t         hostInterface;        /* plain number of interface       */
    u_int16_t        maxNumTXdesc;         /* max number of TX descriptors    */
    u_int16_t        maxNumMCaddr;         /* max number multicast addresses  */
    u_int16_t        numAntennas;          /* number antennas used            */
    u_int16_t        regionCode;           /* region (eg. 0x10 for USA FCC)   */
    unsigned char    macAddr[ETH_ALEN];    /* well known -> AA:BB:CC:DD:EE:FF */
};

struct mwl_priv_stats {
    u_int32_t        hw_failure;           /* generic hardware failure        */
    u_int32_t        txRetrySuccesses;     /* retry success TX                */
    u_int32_t        txMultRetrySucc;      /* multiple retry success TX       */
    u_int32_t        txFailures;           /* failures to sent frames TX      */
    u_int32_t        RTSSuccesses;         /* RTS successes                   */
    u_int32_t        RTSFailures;          /* RTS failure errors              */
    u_int32_t        ackFailures;          /* achknowledge errors RX/TX       */
    u_int32_t        rxDuplicateFrames;    /* duplicated frames while RX      */
    u_int32_t        FCSErrorCount;        /* FCS errors RX/TX                */
    u_int32_t        txWatchDogTimeouts;   /* watchdog timeouts while TX      */
    u_int32_t        rxOverflows;          /* overflow errors while RX        */
    u_int32_t        rxFragErrors;         /* fragmentation errors while RX   */
    u_int32_t        rxMemErrors;          /* memory errors while RX          */
    u_int32_t        pointerErrors;        /* generic pointer errors          */
    u_int32_t        txUnderflows;         /* underflow errors while TX       */
    u_int32_t        txDone;               /* tx done counter                 */
    u_int32_t        txDoneBufTryPut;      /* tx done buf try put counter     */
    u_int32_t        txDoneBufPut;         /* tx done buf put counter         */
    u_int32_t        wait4TxBuf;           /* wait for tx buff counter        */
    u_int32_t        txAttempts;           /* tx attempt counter              */
    u_int32_t        txSuccesses;          /* tx successful counter           */
    u_int32_t        txFragments;          /* tx fragment counter             */
    u_int32_t        txMulticasts;         /* tx multicast counter            */
    u_int32_t        rxNonCtlPkts;         /* rx non control packet counter   */
    u_int32_t        rxMulticasts;         /* rx multicast counter            */
    u_int32_t        rxUndecryptFrames;    /* undecryptable frames while RX   */
    u_int32_t        rxICVErrors;          /* ICV errors while RX             */
    u_int32_t        rxExcludedFrames;     /* excluded frames while RX        */
};

struct mwl_ieee_funcs {
    int  (*mwl_newState)(struct ieee80211com *, enum ieee80211_state, int);
    void (*mwl_nodeCleanup)(struct ieee80211com *, struct ieee80211_node *);
    void (*mwl_nodeCopy)(struct ieee80211com *,
                         struct ieee80211_node *,
                         const struct ieee80211_node *);
    void (*mwl_mgmtRecv)(struct ieee80211com *,
                         struct sk_buff *,
                         struct ieee80211_node *,
                         int, int, u_int32_t);
};

struct mwl_private {
    struct net_device        netDev;          /* the net_device struct        */
    struct net_device_stats  netDevStats;     /* net_device statistics        */
    struct pci_dev          *pPciDev;         /* for access to pci cfg space  */
    void                    *ioBase0;         /* MEM Base Address Register 0  */
    void                    *ioBase1;         /* MEM Base Address Register 1  */
    dma_addr_t               pPhysCmdBuf;     /* pointer to CmdBuf (physical) */
    unsigned short          *pCmdBuf;         /* pointer to CmdBuf (virtual)  */
    struct timer_list        neighborScanTimer; /* AP/neightbor scan timer    */
    mwl_bool_e               usingGProtection;/* currently using Gprot?       */
    mwl_bool_e               isMtuChanged;    /* change may interact with open*/
    mwl_bool_e               isTxTimeout;     /* timeout may collide with scan*/
    mwl_bool_e               inReset;         /* is chip currently resetting  */
    int                      fixedRate;       /* operator configured rate     */
    struct mwl_locks         locks;           /* various spinlocks            */
    struct mwl_desc_data     descData;        /* various descriptor data      */
    struct mwl_hw_data       hwData;          /* Adapter HW specific info     */
    struct mwl_priv_stats    privStats;       /* wireless statistic data      */
    struct mwl_ieee_funcs    parentFuncs;     /* initial default functions    */
    struct mwl_beacon_data   beaconData;
    struct ieee80211com      curr80211com;    /* IEEE 802.11 common           */
    enum ieee80211_opmode    initialOpmode;
    struct ieee80211_channel currChannel;
    struct iw_statistics     wStats;          /* wireless statistic data      */
};

#endif /* MWL_MAIN_H */

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
