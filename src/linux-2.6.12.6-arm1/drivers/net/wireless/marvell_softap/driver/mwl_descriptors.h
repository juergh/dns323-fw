/*******************************************************************************
 *
 * Name:        mwl_descriptors.h
 * Project:     Linux SoftAP for Marvell CB32/CB35 cardbus adapter
 * Version:     $Revision: 1.1.1.1 $
 * Date:        $Date: 2009/08/19 08:02:21 $
 * Author:      Ralph Roesler (rroesler@syskonnect.de)
 * Purpose:     descriptor header file
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

#ifndef MWL_DESCRIPTORS_H
#define MWL_DESCRIPTORS_H

#include <linux/if_ether.h>   
#include <linux/netdevice.h>
#include <linux/skbuff.h>

/*******************************************************************************
 *
 * Generic driver defines
 *
 ******************************************************************************/

extern int mwl_allocateTxRing(struct net_device *netdev);
extern int mwl_allocateRxRing(struct net_device *netdev);
extern int mwl_initTxRing(struct net_device *netdev);
extern int mwl_initRxRing(struct net_device *netdev);
extern void mwl_freeTxRing(struct net_device *netdev);
extern void mwl_freeRxRing(struct net_device *netdev);
extern void mwl_cleanTxRing(struct net_device *netdev);
extern void mwl_cleanRxRing(struct net_device *netdev);
  
#define SOFT_STAT_STALE                               0x80

#define EAGLE_RXD_CTRL_DRIVER_OWN                     0x00
#define EAGLE_RXD_CTRL_OS_OWN                         0x04
#define EAGLE_RXD_CTRL_DMA_OWN                        0x80

#define EAGLE_RXD_STATUS_IDLE                         0x00
#define EAGLE_RXD_STATUS_OK                           0x01
#define EAGLE_RXD_STATUS_MULTICAST_RX                 0x02
#define EAGLE_RXD_STATUS_BROADCAST_RX                 0x04
#define EAGLE_RXD_STATUS_FRAGMENT_RX                  0x08

#define EAGLE_TXD_STATUS_IDLE                   0x00000000
#define EAGLE_TXD_STATUS_USED                   0x00000001 
#define EAGLE_TXD_STATUS_OK                     0x00000001
#define EAGLE_TXD_STATUS_OK_RETRY               0x00000002
#define EAGLE_TXD_STATUS_OK_MORE_RETRY          0x00000004
#define EAGLE_TXD_STATUS_MULTICAST_TX           0x00000008
#define EAGLE_TXD_STATUS_BROADCAST_TX           0x00000010
#define EAGLE_TXD_STATUS_FAILED_LINK_ERROR      0x00000020
#define EAGLE_TXD_STATUS_FAILED_EXCEED_LIMIT    0x00000040
#define EAGLE_TXD_STATUS_FAILED_AGING           0x00000080
#define EAGLE_TXD_STATUS_FW_OWNED               0x80000000

typedef struct _mwl_txdesc_t __attribute__ ((packed)) mwl_txdesc_t;
struct _mwl_txdesc_t {
    u_int32_t        Status;
    u_int8_t         DataRate;
    u_int8_t         TxPriority;
    u_int16_t        QosCtrl;
    u_int32_t        PktPtr;
    u_int16_t        PktLen;
    u_int8_t         DestAddr[6];
    u_int32_t        pPhysNext;
    u_int32_t        SapPktInfo;
    u_int32_t        Reserved2;
    struct sk_buff  *pSkBuff;
    mwl_txdesc_t    *pNext;
    u_int32_t        SoftStat;
    struct ieee80211_node *pNode;  
};

typedef struct _mwl_rxdesc_t __attribute__ ((packed)) mwl_rxdesc_t;
struct _mwl_rxdesc_t {
    u_int8_t         RxControl;      /* the control element of the desc    */
    u_int8_t         RSSI;           /* received signal strengt indication */
    u_int8_t         Status;         /* status field containing USED bit   */
    u_int8_t         Channel;        /* channel this pkt was received on   */
    u_int16_t        PktLen;         /* total length of received data      */
    u_int8_t         SQ2;            /* unused at the moment               */
    u_int8_t         Rate;           /* received data rate                 */
    u_int32_t        pPhysBuffData;  /* physical address of payload data   */
    u_int32_t        pPhysNext;      /* physical address of next RX desc   */ 
    u_int16_t        QosCtrl;        /* received QosCtrl field variable    */
    u_int16_t        Reserved;       /* like name states                   */
    struct sk_buff  *pSkBuff;        /* associated sk_buff for Linux       */
    void            *pBuffData;      /* virtual address of payload data    */ 
    mwl_rxdesc_t    *pNext;          /* virtual address of next RX desc    */
};

#endif /* MWL_DESCRIPTORS_H */

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
