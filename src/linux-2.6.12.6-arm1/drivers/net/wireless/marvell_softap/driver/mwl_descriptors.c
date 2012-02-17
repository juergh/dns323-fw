/*******************************************************************************
 *
 * Name:        mwl_descriptors.c
 * Project:     Linux SoftAP for Marvell CB32/CB35 cardbus adapter
 * Version:     $Revision: 1.1.1.1 $
 * Date:        $Date: 2009/08/19 08:02:21 $
 * Author:      Ralph Roesler (rroesler@syskonnect.de)
 * Purpose:     descriptor functions
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

#include "mwl_descriptors.h"
#include "mwl_main.h"
#include "mwl_debug.h"

/*******************************************************************************
 *
 * Definitions (defines, typedefs, prototypes etc.)
 *
 ******************************************************************************/

#define MAX_NUM_TX_RING_BYTES  MAX_NUM_TX_DESC * sizeof(mwl_txdesc_t)
#define MAX_NUM_RX_RING_BYTES  MAX_NUM_RX_DESC * sizeof(mwl_rxdesc_t)

#define FIRST_TXD mwlp->descData.pTxRing[0]
#define CURR_TXD  mwlp->descData.pTxRing[currDescr]
#define NEXT_TXD  mwlp->descData.pTxRing[currDescr+1]
#define LAST_TXD  mwlp->descData.pTxRing[MAX_NUM_TX_DESC-1]

#define FIRST_RXD mwlp->descData.pRxRing[0]
#define CURR_RXD  mwlp->descData.pRxRing[currDescr]
#define NEXT_RXD  mwlp->descData.pRxRing[currDescr+1]
#define LAST_RXD  mwlp->descData.pRxRing[MAX_NUM_RX_DESC-1]

/*******************************************************************************
 *
 * Global functions
 *
 ******************************************************************************/

int
mwl_allocateTxRing(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;

    MWL_DBG_ENTER_INFO(DBG_CLASS_DESC, "allocating %i (0x%x) bytes",
        MAX_NUM_TX_RING_BYTES, MAX_NUM_TX_RING_BYTES);

    mwlp->descData.pTxRing = 
       (mwl_txdesc_t *) pci_alloc_consistent(mwlp->pPciDev,
                                             MAX_NUM_TX_RING_BYTES,
                                             &mwlp->descData.pPhysTxRing);
    if (mwlp->descData.pTxRing == NULL) {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_DESC, "no mem");
        return MWL_ERROR;
    }
    memset(mwlp->descData.pTxRing, 0x00, MAX_NUM_TX_RING_BYTES);
    MWL_DBG_EXIT_INFO(DBG_CLASS_DESC, "TX ring vaddr: 0x%x paddr: 0x%x", 
        mwlp->descData.pTxRing, mwlp->descData.pPhysTxRing);
    return MWL_OK;
}

int
mwl_initTxRing(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    int currDescr;

    MWL_DBG_ENTER_INFO(DBG_CLASS_DESC, 
        "initializing %i descriptors", MAX_NUM_TX_DESC);

    if (mwlp->descData.pTxRing != NULL) {
        for (currDescr = 0; currDescr < MAX_NUM_TX_DESC; currDescr++) {
            CURR_TXD.Status    = ENDIAN_SWAP32(EAGLE_TXD_STATUS_IDLE);
            CURR_TXD.pNext     = &NEXT_TXD; /* v.addr -> no swap! */
            CURR_TXD.pPhysNext = 
                ENDIAN_SWAP32((u_int32_t) mwlp->descData.pPhysTxRing + 
                              ((currDescr+1)*sizeof(mwl_txdesc_t)));
            MWL_DBG_INFO(DBG_CLASS_DESC, 
                "desc: %i status: 0x%x (%i) vnext: 0x%p pnext: 0x%x",
                currDescr, EAGLE_TXD_STATUS_IDLE, EAGLE_TXD_STATUS_IDLE,
                CURR_TXD.pNext, ENDIAN_SWAP32(CURR_TXD.pPhysNext));
        }
        LAST_TXD.pNext = &FIRST_TXD; /* v.addr -> no swap! */
        LAST_TXD.pPhysNext = 
            ENDIAN_SWAP32((u_int32_t) mwlp->descData.pPhysTxRing);
        mwlp->descData.pStaleTxDesc = &FIRST_TXD; /* v.addr->no swap!*/
        mwlp->descData.pNextTxDesc  = &FIRST_TXD; /* v.addr->no swap!*/

        MWL_DBG_EXIT_INFO(DBG_CLASS_DESC, 
            "last desc vnext: 0x%p pnext: 0x%x pstale 0x%x vfirst 0x%x",
            LAST_TXD.pNext, ENDIAN_SWAP32(LAST_TXD.pPhysNext),
            mwlp->descData.pStaleTxDesc, mwlp->descData.pNextTxDesc);
        return MWL_OK;
    }
    MWL_DBG_EXIT_ERROR(DBG_CLASS_DESC, "no valid TX mem");
    return MWL_ERROR;
}

void
mwl_cleanTxRing(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    int cleanedTxDescr = 0;
    int currDescr;

    MWL_DBG_ENTER(DBG_CLASS_DESC);

    if (mwlp->descData.pTxRing != NULL) {
        for (currDescr = 0; currDescr < MAX_NUM_RX_DESC; currDescr++) {
            if (CURR_TXD.pSkBuff != NULL) {
                MWL_DBG_INFO(DBG_CLASS_DESC, 
                    "unmapped and free'd desc %i vaddr: 0x%p paddr: 0x%x",
                    currDescr, CURR_TXD.pSkBuff->data, 
                    ENDIAN_SWAP32(CURR_TXD.PktPtr));
                pci_unmap_single(mwlp->pPciDev,
                                 ENDIAN_SWAP32(CURR_TXD.PktPtr),
                                 CURR_TXD.pSkBuff->len,
                                 PCI_DMA_TODEVICE);
                dev_kfree_skb_any(CURR_TXD.pSkBuff);
                CURR_TXD.pSkBuff   = NULL;
                CURR_TXD.PktPtr    = 0;
                CURR_TXD.PktLen    = 0;
                CURR_TXD.SoftStat &= ~SOFT_STAT_STALE;
                CURR_TXD.Status    = ENDIAN_SWAP32(EAGLE_TXD_STATUS_IDLE);
                cleanedTxDescr++;
            }
        }
    }
    MWL_DBG_EXIT_INFO(DBG_CLASS_DESC, "cleaned %i TX descr", cleanedTxDescr);
}

int
mwl_allocateRxRing(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;

    MWL_DBG_ENTER_INFO(DBG_CLASS_DESC, "allocating %i (0x%x) bytes",
        MAX_NUM_RX_RING_BYTES, MAX_NUM_RX_RING_BYTES);

    mwlp->descData.pRxRing = 
       (mwl_rxdesc_t *) pci_alloc_consistent(mwlp->pPciDev,
                                             MAX_NUM_RX_RING_BYTES,
                                             &mwlp->descData.pPhysRxRing);
    if (mwlp->descData.pRxRing == NULL) {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_DESC, "no mem");
        return MWL_ERROR;
    }
    memset(mwlp->descData.pRxRing, 0x00, MAX_NUM_RX_RING_BYTES);
    MWL_DBG_EXIT_INFO(DBG_CLASS_DESC, "RX ring vaddr: 0x%x paddr: 0x%x", 
        mwlp->descData.pRxRing, mwlp->descData.pPhysRxRing);
    return MWL_OK;
}

int
mwl_initRxRing(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    int currDescr;

    MWL_DBG_ENTER_INFO(DBG_CLASS_DESC, 
        "initializing %i descriptors", MAX_NUM_RX_DESC);

    if (mwlp->descData.pRxRing != NULL) {
        mwlp->descData.rxBufSize = netdev->mtu + NUM_EXTRA_RX_BYTES;
        for (currDescr = 0; currDescr < MAX_NUM_RX_DESC; currDescr++) {
            CURR_RXD.pSkBuff   = alloc_skb(mwlp->descData.rxBufSize,GFP_ATOMIC);
            CURR_RXD.RxControl = EAGLE_RXD_CTRL_DRIVER_OWN; 
            CURR_RXD.Status    = EAGLE_RXD_STATUS_OK; 
            CURR_RXD.QosCtrl   = 0x0000;
            CURR_RXD.Channel   = 0x00;
            CURR_RXD.RSSI      = 0x00; 
            CURR_RXD.SQ2       = 0x00;

            if (CURR_RXD.pSkBuff != NULL) {
                CURR_RXD.PktLen    = netdev->mtu + NUM_EXTRA_RX_BYTES;
                CURR_RXD.pBuffData = CURR_RXD.pSkBuff->data; /* no swap! */
                CURR_RXD.pPhysBuffData = 
                    ENDIAN_SWAP32(pci_map_single(mwlp->pPciDev,
                                                 CURR_RXD.pSkBuff->data,
                                                 mwlp->descData.rxBufSize,
                                                 PCI_DMA_FROMDEVICE));
                CURR_RXD.pNext = &NEXT_RXD; /* v.addr->no swap! */
                CURR_RXD.pPhysNext = 
                    ENDIAN_SWAP32((u_int32_t) mwlp->descData.pPhysRxRing + 
                                  ((currDescr+1)*sizeof(mwl_rxdesc_t)));
                MWL_DBG_INFO(DBG_CLASS_DESC, 
                    "desc: %i status: 0x%x (%i) len: 0x%x (%i)",
                    currDescr, EAGLE_TXD_STATUS_IDLE, EAGLE_TXD_STATUS_IDLE,
                    mwlp->descData.rxBufSize, mwlp->descData.rxBufSize);
                MWL_DBG_INFO(DBG_CLASS_DESC, 
                    "desc: %i vnext: 0x%p pnext: 0x%x", currDescr,
                    CURR_RXD.pNext, ENDIAN_SWAP32(CURR_RXD.pPhysNext));
            } else {
                MWL_DBG_EXIT_ERROR(DBG_CLASS_DESC, 
                    "desc %i: no skbuff available", currDescr);
                return MWL_ERROR;
            }
        }
        LAST_RXD.pPhysNext = 
            ENDIAN_SWAP32((u_int32_t) mwlp->descData.pPhysRxRing);
        LAST_RXD.pNext             = &FIRST_RXD; /* v.addr->no swap! */
        mwlp->descData.pNextRxDesc = &FIRST_RXD; /* v.addr->no swap! */

        MWL_DBG_EXIT_INFO(DBG_CLASS_DESC, 
            "last desc vnext: 0x%p pnext: 0x%x vfirst 0x%x",
            LAST_RXD.pNext, ENDIAN_SWAP32(LAST_RXD.pPhysNext),
            mwlp->descData.pNextRxDesc);
        return MWL_OK;
    }
    MWL_DBG_EXIT_ERROR(DBG_CLASS_DESC, "no valid RX mem");
    return MWL_ERROR;
}

void
mwl_cleanRxRing(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    int currDescr;

    MWL_DBG_ENTER(DBG_CLASS_DESC);

    if (mwlp->descData.pRxRing != NULL) {
        for (currDescr = 0; currDescr < MAX_NUM_RX_DESC; currDescr++) {
            if (CURR_RXD.pSkBuff != NULL) {
                if (skb_shinfo(CURR_RXD.pSkBuff)->nr_frags) {
                    skb_shinfo(CURR_RXD.pSkBuff)->nr_frags = 0;
                }
                if (skb_shinfo(CURR_RXD.pSkBuff)->frag_list) {
                    skb_shinfo(CURR_RXD.pSkBuff)->frag_list = NULL;
                }
                pci_unmap_single(mwlp->pPciDev, 
                                 ENDIAN_SWAP32(CURR_RXD.pPhysBuffData),
                                 mwlp->descData.rxBufSize,
                                 PCI_DMA_FROMDEVICE);
                dev_kfree_skb_any(CURR_RXD.pSkBuff);
                MWL_DBG_INFO(DBG_CLASS_DESC, 
                    "unmapped+free'd desc %i vaddr: 0x%p paddr: 0x%x len: %i",
                    currDescr, CURR_RXD.pBuffData, 
                    ENDIAN_SWAP32(CURR_RXD.pPhysBuffData),
                    mwlp->descData.rxBufSize);
                CURR_RXD.pBuffData = NULL;
                CURR_RXD.pSkBuff = NULL;
            }
        }
    }
    MWL_DBG_EXIT(DBG_CLASS_DESC);
}

void
mwl_freeTxRing(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;

    MWL_DBG_ENTER(DBG_CLASS_DESC);

    if (mwlp->descData.pTxRing != NULL) {
        pci_free_consistent(mwlp->pPciDev, 
                            MAX_NUM_TX_RING_BYTES,
                            mwlp->descData.pTxRing, 
                            mwlp->descData.pPhysTxRing);
        mwlp->descData.pTxRing = NULL;
    }
    mwlp->descData.pStaleTxDesc = NULL; 
    mwlp->descData.pNextTxDesc  = NULL;
    MWL_DBG_EXIT(DBG_CLASS_DESC);
}

void
mwl_freeRxRing(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;

    MWL_DBG_ENTER(DBG_CLASS_DESC);

    if (mwlp->descData.pRxRing != NULL) {
        mwl_cleanRxRing(netdev); 
        pci_free_consistent(mwlp->pPciDev, 
                            MAX_NUM_RX_RING_BYTES,
                            mwlp->descData.pRxRing, 
                            mwlp->descData.pPhysRxRing);
        mwlp->descData.pRxRing = NULL;
    }
    mwlp->descData.pNextRxDesc = NULL;
    MWL_DBG_EXIT(DBG_CLASS_DESC);
}

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
