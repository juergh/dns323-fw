/*******************************************************************************
 *
 * Name:        mwl_xmitrecv.c
 * Project:     Linux SoftAP for Marvell CB32/CB35 cardbus adapter
 * Version:     $Revision: 1.1.1.1 $
 * Date:        $Date: 2009/08/19 08:02:21 $
 * Author:      Ralph Roesler (rroesler@syskonnect.de)
 * Purpose:     transmit receive functions
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
#include "mwl_registers.h"
#include "mwl_descriptors.h"
#include "mwl_main.h"
#include "mwl_xmitrecv.h"
#include "mwl_hostcmd.h"

/*******************************************************************************
 *
 * Definitions (defines, typedefs, prototypes etc.)
 *
 ******************************************************************************/

static int mwl_xmit(struct net_device *, struct ieee80211_node *, struct sk_buff *, int);
static void mwl_evaluateNewRssi(struct mwl_node *, int);
static void mwl_evaluateNewRecvRate(struct ieee80211_node *, u_int8_t);

#define CURR_TXD mwlp->descData.pStaleTxDesc

#define MGMT_FRAME IEEE80211_FC0_TYPE_MGT >> IEEE80211_FC0_TYPE_SHIFT
#define DATA_FRAME IEEE80211_FC0_TYPE_DATA >> IEEE80211_FC0_TYPE_SHIFT
#define CTL_FRAME  IEEE80211_FC0_TYPE_CTL >> IEEE80211_FC0_TYPE_SHIFT
#define IS_CTL_FRAME(wh) \
    ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_CTL)
#define IS_DATA_FRAME(wh) \
    ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_DATA)
#define IS_MGMT_FRAME(wh) \
    ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_MGT)
#define IS_PROBE_RESP(wh) \
    ((wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) == IEEE80211_FC0_SUBTYPE_PROBE_RESP)

#define IS_WEP(wh) \
    ((wh->i_fc[1] & IEEE80211_FC1_WEP) == IEEE80211_FC1_WEP)
#define IS_EAPOL(skb) \
    ((skb->data[24] == 0xaa) && (skb->data[25] == 0xaa) &&  \
     (skb->data[30] == 0x88) && (skb->data[31] == 0x8e))
#define IS_NOT_SECURE(skb) \
    ((skb->data[37] & 0x02) != 0x02)

/*******************************************************************************
 *
 * Global functions
 *
 ******************************************************************************/

int
mwl_dataXmit(struct sk_buff *skb, struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;
    struct ieee80211_node *ni = NULL;
    unsigned long flags;

    MWL_DBG_ENTER(DBG_CLASS_RXTX);

    if ((netdev->flags & IFF_RUNNING) == 0) {
        mwlp->netDevStats.tx_dropped++;
        MWL_DBG_EXIT_ERROR(DBG_CLASS_RXTX, "%s: itf not running", netdev->name);
        return -ENETDOWN;
    }

    spin_lock_irqsave(&mwlp->locks.xmitLock, flags);
    if (ic->ic_state != IEEE80211_S_RUN) {
        MWL_DBG_ERROR(DBG_CLASS_RXTX,"discard frame (%s)",
            ieee80211_state_name[ic->ic_state]);
        mwlp->netDevStats.tx_dropped++;
        goto error;
    }

    if ((skb = ieee80211_encap(ic, skb, &ni)) == NULL) {
        MWL_DBG_ERROR(DBG_CLASS_RXTX, "encap error");
        mwlp->netDevStats.tx_errors++;
        goto error;
    }

    if (mwl_xmit(netdev, ni, skb, DATA_FRAME)) {
        MWL_DBG_ERROR(DBG_CLASS_RXTX, "could not xmit");
        mwlp->netDevStats.tx_errors++;
        goto error;
    }

    mwlp->netDevStats.tx_packets++;
    mwlp->netDevStats.tx_bytes += skb->len;
    spin_unlock_irqrestore(&mwlp->locks.xmitLock, flags);
    MWL_DBG_EXIT(DBG_CLASS_RXTX);
    return 0;

error:
    if (ni && ni != ic->ic_bss) {
        ieee80211_free_node(ic, ni);
    }
    if (skb) {
        dev_kfree_skb(skb);
    }
    spin_unlock_irqrestore(&mwlp->locks.xmitLock, flags);
    MWL_DBG_EXIT_ERROR(DBG_CLASS_RXTX, NULL);
    return 0;
}

/*
** The referenced node pointer is maintained in the control block of 
** the sk_buff. This is placed there by ieee80211_mgmt_output because 
** we need to hold the ref with the frame when cleaning up xmit later...
*/
int
mwl_mgmtXmit(struct ieee80211com *ic, struct sk_buff *skb)
{
    struct net_device *netdev = ic->ic_dev;
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211_frame *wh = (struct ieee80211_frame *) skb->data;
    struct ieee80211_cb *cb = (struct ieee80211_cb *) skb->cb;
    struct ieee80211_node *ni = cb->ni;

    MWL_DBG_ENTER(DBG_CLASS_RXTX);

    spin_lock(&mwlp->locks.xmitLock);
    if (mwlp->inReset) {
        goto error;
    }

    if ((netdev->flags & IFF_RUNNING) == 0) {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_RXTX, "%s: itf not running", netdev->name);
        mwlp->netDevStats.tx_dropped++;
        goto error;
    }

    if (IS_PROBE_RESP(wh)) {
        u_int64_t tsf = 100; /* adjust 100us delay to xmit */
        u_int32_t *tstamp =  (u_int32_t *) &wh[1];

        tstamp[0] = cpu_to_le32(tsf & 0xffffffff);
        tstamp[1] = cpu_to_le32(tsf >> 32);
    }

    if (mwl_xmit(netdev, ni, skb, MGMT_FRAME)) {
        MWL_DBG_ERROR(DBG_CLASS_RXTX, "could not xmit");
        mwlp->netDevStats.tx_errors++;
        goto error;
    }

    mwlp->netDevStats.tx_packets++;
    mwlp->netDevStats.tx_bytes += skb->len;
    spin_unlock(&mwlp->locks.xmitLock);
    MWL_DBG_EXIT(DBG_CLASS_RXTX);
    return 0;

error:
    if (ni && ni != ic->ic_bss) {
        ieee80211_free_node(ic, ni);
    }
    if (skb) {
        dev_kfree_skb(skb);
    }
    spin_unlock(&mwlp->locks.xmitLock);
    MWL_DBG_EXIT_ERROR(DBG_CLASS_RXTX, NULL);
    return -EFAULT;
}

void
mwl_xmitComplete(struct net_device *netdev)
{
    static mwl_bool_e isFunctionBusy = MWL_FALSE;
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;
    u_int8_t rate2Rate11AG[] = { 0,1,2,3,0,4,5,6,7,8,9,10,11 };
    u_int8_t rate2Rate11B[] = { 0,1,2,3 };
    struct ieee80211_node *ni;

    MWL_DBG_ENTER(DBG_CLASS_RXTX);

    if (!isFunctionBusy) {
        isFunctionBusy = MWL_TRUE;
        while ((CURR_TXD->SoftStat & SOFT_STAT_STALE) &&
               (!(CURR_TXD->Status & ENDIAN_SWAP32(EAGLE_TXD_STATUS_FW_OWNED)))) {
            if (CURR_TXD->Status & ENDIAN_SWAP32(EAGLE_TXD_STATUS_OK)) {
                ni = CURR_TXD->pNode;
                if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
                    if (ni != ic->ic_bss) {
                        ieee80211_free_node(ic, ni); /* reclaim node ref */
                    }
                } else {
                    if (CURR_TXD->DataRate != 0) {
                        if (ic->ic_curmode == IEEE80211_MODE_11B) {
                            ni->ni_txrate = rate2Rate11B[CURR_TXD->DataRate];
                        } else {
                            ni->ni_txrate = rate2Rate11AG[CURR_TXD->DataRate];
                        }
                    } else {
                        ni->ni_txrate = 2; /* 1mbps dataframes default */
                    }
                }
            }

            pci_unmap_single(mwlp->pPciDev, 
                             ENDIAN_SWAP32(CURR_TXD->PktPtr),
                             CURR_TXD->pSkBuff->len,
                             PCI_DMA_TODEVICE);

            dev_kfree_skb_any(CURR_TXD->pSkBuff);

            CURR_TXD->pSkBuff = NULL;
            CURR_TXD->Status = ENDIAN_SWAP32(EAGLE_TXD_STATUS_IDLE);
            CURR_TXD->PktLen = 0;
            CURR_TXD->SoftStat &= ~SOFT_STAT_STALE;
            CURR_TXD = CURR_TXD->pNext;
        }
        isFunctionBusy = MWL_FALSE;
    }
    MWL_DBG_EXIT(DBG_CLASS_RXTX);
}

void
mwl_mgmtRecv(struct ieee80211com *ic, struct sk_buff *skb, struct ieee80211_node *ni, int subtype, int rssi, u_int32_t rstamp)
{
    struct mwl_private *mwlp = ic->ic_dev->priv;
    struct mwl_node *mwln = (struct mwl_node *) ni;
    struct ieee80211_frame *wh = (struct ieee80211_frame *) skb->data;
    u_int8_t rates[] = {0x82,0x84,0x8b,0x96,12,18,24,36,48,72,96,108};
    struct ieee80211_rateset *rs;

    MWL_DBG_ENTER(DBG_CLASS_RXTX);

    /* 
    ** Call up first generic 802.11 layer so subsequent work can use 
    ** information potentially stored in the node (e.g. for ibss merge)
    */
    mwlp->parentFuncs.mwl_mgmtRecv(ic, skb, ni, subtype, rssi, rstamp);
    switch (subtype) {
        case IEEE80211_FC0_SUBTYPE_DISASSOC:
            mwln->lastRxRate = 0;
            MWL_DBG_INFO(DBG_CLASS_RXTX, "set last rx rate back to 0");
            break;
        case IEEE80211_FC0_SUBTYPE_BEACON:  /* fall thru */
            if (ic->ic_opmode == IEEE80211_M_STA) {
                if (IEEE80211_ADDR_EQ(wh->i_addr3, ni->ni_bssid)) {
                    if ((wh->i_addr3[0] == 0x00) &&
                        (wh->i_addr3[1] == 0x50) &&
                        (wh->i_addr3[2] == 0x43)) {
                        rs = &ni->ni_rates;
                        if (rs->rs_rates[4] != 12) {
                            /* if Marvell AP fix display rate */
                            memcpy(&rs->rs_rates[0], &rates[0], sizeof(rates));
                        }
                    }
                }
                if (IEEE80211_ADDR_EQ(ni->ni_bssid, ic->ic_bss->ni_bssid)) {
                    if (ic->ic_flags & IEEE80211_F_USEPROT) {
                        if (mwlp->usingGProtection == MWL_FALSE) {
                            mwlp->usingGProtection = MWL_TRUE;
                            MWL_DBG_INFO(DBG_CLASS_RXTX, "will set Gprot");
                            if (mwl_setFwGProtMode(&(mwlp->netDev),MWL_ENABLE)){
                                MWL_DBG_ERROR(DBG_CLASS_RXTX, "G prot");
                            }
                        }
                    }
                }
            }
            if (mwln->beaconData.pSkBuff == NULL) {
                mwln->beaconData.pSkBuff=alloc_skb((skb->len),GFP_ATOMIC);
                if (mwln->beaconData.pSkBuff != NULL) {
                    memset(mwln->beaconData.pSkBuff->data,0x00,skb->len);
                    memcpy(&mwln->beaconData.pSkBuff->data[0],
                           &skb->data[0], skb->len);
                    mwln->beaconData.pSkBuff->len = skb->len;
                    if (IEEE80211_ADDR_EQ(wh->i_addr3, ni->ni_bssid)) {
                        // if (mwl_setFwFinalizeJoin(&(mwlp->netDev), ni, 1)) {
                        //     MWL_DBG_ERROR(DBG_CLASS_RXTX, "finalize join");
                        // }
                    }
                }
            }
            break;
        case IEEE80211_FC0_SUBTYPE_PROBE_RESP: /* fall thru */
        default:
            break;
    }
    MWL_DBG_EXIT(DBG_CLASS_RXTX);
}

void
mwl_recv(struct net_device *netdev)
{
    static mwl_bool_e isFunctionBusy = MWL_FALSE;
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;
    struct ieee80211_node *ni = NULL;
    struct mwl_node *mwln = NULL;
    mwl_rxdesc_t *pCurrent = mwlp->descData.pNextRxDesc;
    int receivedHandled = 0;
    u_int32_t rxRdPtr;
    u_int32_t rxWrPtr;
    struct sk_buff *pRxSkBuff;
    void *pCurrentData;
    u_int8_t rxRate;
    int rxCount;
    int rssi;

    MWL_DBG_ENTER(DBG_CLASS_RXTX);

    if (isFunctionBusy) {
        goto rx_end; /* skip any action if already in this function */
    }
    isFunctionBusy = MWL_TRUE;

    rxRdPtr = readl(mwlp->ioBase0 + mwlp->descData.rxDescRead);
    rxWrPtr = readl(mwlp->ioBase0 + mwlp->descData.rxDescWrite);

    while ((rxRdPtr != rxWrPtr) && (receivedHandled < MAX_NUM_RX_DESC)) {
        pRxSkBuff = alloc_skb(netdev->mtu + NUM_EXTRA_RX_BYTES, GFP_ATOMIC);
        if (pRxSkBuff == NULL) {
            pCurrent->RxControl = EAGLE_RXD_CTRL_DRIVER_OWN; /* 1byte(no swap)*/
            break; /* maybe we can think of an error counter increment? */
        }

        skb_reserve(pRxSkBuff, MIN_BYTES_HEADROOM);
        pCurrentData = pCurrent->pBuffData;
        rxCount = ENDIAN_SWAP16(pCurrent->PktLen);
        rxRate = pCurrent->Rate;
#if 0 
        pci_dma_sync_single(mwlp->pPciDev,
                            (dma_addr_t) ENDIAN_SWAP32(pCurrent->pPhysBuffData),
                            (netdev->mtu + NUM_EXTRA_RX_BYTES),
                            PCI_DMA_FROMDEVICE);
#endif

        memcpy(pRxSkBuff->data, 
               pCurrentData + OFFS_RXFWBUFF_IEEE80211HEADER, 
               NBR_BYTES_IEEE80211HEADER);

        memcpy(&pRxSkBuff->data[OFFS_IEEE80211PAYLOAD], 
               pCurrentData+OFFS_RXFWBUFF_IEEE80211PAYLOAD, 
               rxCount - OFFS_RXFWBUFF_IEEE80211PAYLOAD);

        if ((rssi = ((int) pCurrent->RSSI & 0xff)) > 50 ) {
            rssi = 50;
        }
        rssi *= 2;

        if (ic->ic_opmode == IEEE80211_M_MONITOR) {
            /* 
            ** Monitor mode: discard anything shorter than an ack or cts, 
            ** clean the skbuff, fabricate the Prism header existing tools
            ** expect, and dispatch.    
            */
            if (rxCount < IEEE80211_ACK_LEN) {
                MWL_DBG_WARN(DBG_CLASS_RXTX, "invalid receive len");
                mwlp->netDevStats.rx_length_errors++;
                dev_kfree_skb_any(pRxSkBuff);
                goto rx_next;
            }
        }

        /* 
        ** From this point on we assume the frame is at least
        ** as large as ieee80211_frame_min; verify that.    
        */
        if (rxCount < IEEE80211_MIN_LEN) {
            MWL_DBG_WARN(DBG_CLASS_RXTX, "invalid receive len");
            mwlp->netDevStats.rx_length_errors++;
            dev_kfree_skb_any(pRxSkBuff);
            goto rx_next;
        }

        /* 
        ** normal buffer received: Locate the node for sender, track state,
        ** and then pass the (referenced) node up to the 802.11 layer for 
        ** its use. We are required to pass some node so we fall back to 
        ** ic_bss when this frame is from an unknown sender. The 802.11 
        ** layer knows this means the sender wasn't in the node table and 
        ** acts accordingly. Note also that by convention we do not reference
        ** count ic_bss, only other nodes (ic_bss is never free'd) 
        */
        skb_put(pRxSkBuff, rxCount - 8); // ommit 2 byte len + 6 ADDR4
        mwlp->netDevStats.rx_bytes += pRxSkBuff->len;
        if (ic->ic_opmode != IEEE80211_M_STA) {
            struct ieee80211_frame_min *wh = 
                (struct ieee80211_frame_min *) pRxSkBuff->data;
            if (IS_CTL_FRAME(wh)) {
                ni = ieee80211_find_node(ic, wh->i_addr1);
            } else {
                ni = ieee80211_find_node(ic, wh->i_addr2);
            }
            if (ni == NULL) {
                ni = ic->ic_bss;
            }
            if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
                if (IS_DATA_FRAME(wh)) {
                    mwl_evaluateNewRecvRate(ni, rxRate);
                }
            }
        } else {
            ni = ic->ic_bss;
        }

        MWL_DBG_INFO(DBG_CLASS_RXTX, 
            "received %i bytes with rssi of %i at rate %i (0x%x)", 
             pRxSkBuff->len, rssi, rxRate, rxRate);
        ieee80211_input(ic, pRxSkBuff, ni, rssi, 0);

        mwln = (struct mwl_node *) ni;
        if (rssi) {
            mwl_evaluateNewRssi(mwln, rssi);
        }

        if (ni != ic->ic_bss) {
            ieee80211_free_node(ic, ni); /* reclaim node reference */
        }

rx_next:
        receivedHandled++;
        pCurrent->RxControl = EAGLE_RXD_CTRL_DRIVER_OWN;
        rxRdPtr = ENDIAN_SWAP32(pCurrent->pPhysNext);
        pCurrent = pCurrent->pNext;
    }
    writel(rxRdPtr, mwlp->ioBase0 + mwlp->descData.rxDescRead);
    mwlp->descData.pNextRxDesc = pCurrent;
    isFunctionBusy = MWL_FALSE;
rx_end:
    MWL_DBG_EXIT(DBG_CLASS_RXTX);
}

void
mwl_checkFwXmitBuffers(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    mwl_txdesc_t *pCurrDesc = mwlp->descData.pTxRing;
    unsigned int numCheckedDesc = 0;
    unsigned int dummy;

    MWL_DBG_ENTER(DBG_CLASS_RXTX);

    do {
        if ((pCurrDesc->Status & EAGLE_TXD_STATUS_FW_OWNED) == 
                EAGLE_TXD_STATUS_FW_OWNED) {
            printk("descriptor 0x%p is still owned by FW!\n", pCurrDesc);
        }
        numCheckedDesc++;
        pCurrDesc = pCurrDesc->pNext;
    } while (pCurrDesc != mwlp->descData.pTxRing);

    writel(MACREG_H2ARIC_BIT_PPA_READY,
        mwlp->ioBase1 + MACREG_REG_H2A_INTERRUPT_EVENTS);
    dummy = readl(mwlp->ioBase1 + MACREG_REG_INT_CODE);

    MWL_DBG_EXIT(DBG_CLASS_RXTX);
}

/*******************************************************************************
 *
 * Local functions
 *
 ******************************************************************************/

static int
mwl_xmit(struct net_device *netdev, struct ieee80211_node *ni, struct sk_buff *skb, int type)
{
    struct mwl_private *mwlp = netdev->priv;
    struct mwl_node *mwln = (struct mwl_node *) ni;
    struct ieee80211com *ic = &mwlp->curr80211com;
    struct ieee80211_frame *wh = (struct ieee80211_frame *) skb->data;
    int hdrlen = ieee80211_anyhdrsize(wh);
    int pktlen = skb->len;
    int keyidx = -1;
    unsigned int dummy;
    u_int16_t *pFwLen;
    unsigned char buffer[IEEE80211_MAX_LEN + NBR_BYTES_ADD_TXFWINFO];
    const struct ieee80211_cipher *cip;
    struct ieee80211_key *k;

    MWL_DBG_ENTER(DBG_CLASS_RXTX);

    /* 
    ** If we've bumped into the end of the transmit ring, we have to
    ** stall the output queue and wait for a transmit complete interrupt 
    ** to call service(). 
    */
    if (mwlp->descData.pNextTxDesc->SoftStat & SOFT_STAT_STALE) {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_RXTX, "try again later");
        return EAGAIN; /* try again... */
    }

    if (type == DATA_FRAME) {
        if (IS_WEP(wh)) {
            if (IS_EAPOL(skb)) {
                if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
                    if (ic->ic_flags & IEEE80211_F_WPA) {
                        if ((ic->ic_flags & IEEE80211_F_WPA2) || 
                            (IS_NOT_SECURE(skb))) {
                            wh->i_fc[1] = (wh->i_fc[1] & ~IEEE80211_FC1_WEP);
                            goto startPassingBufferToFw;
			}
                    }
                }
            }

            /* 
            ** Construct the 802.11 header+trailer for an encrypted 
            ** frame. The only reason this can fail is because of an
            ** unknown or unsupported cipher/key type.            
            */ 
            MWL_DBG_INFO(DBG_CLASS_RXTX, "have a WEP encrypted frame");
            if ((k = ieee80211_crypto_encap(ic, ni, skb)) == NULL) {
                /* 
                ** This can happen when the key is yanked after the 
                ** frame was queued. Just discard the frame; the  
                ** 802.11 layer counts failures and provides      
                ** debugging/diagnostics.                        
                */
                MWL_DBG_EXIT_ERROR(DBG_CLASS_RXTX, "crypto encap");
                return EIO;
            }

            /* 
            ** Adjust the packet + header lengths for the crypto 
            ** additions and calculate the h/w key index.  When 
            ** a s/w mic is done the frame will have had any mic
            ** added to it prior to entry so skb->len above will
            ** account for it. Otherwise we need to add it to the
            ** packet length.                                   
            */
            cip = k->wk_cipher;
            hdrlen += cip->ic_header;
            pktlen += cip->ic_header + cip->ic_trailer;
            if ((k->wk_flags & IEEE80211_KEY_SWMIC) == 0) {
                pktlen += cip->ic_miclen;
            }
            keyidx = k->wk_keyix;
            /* packet header may moved, reset our local pointer  */
            wh = (struct ieee80211_frame *) skb->data;
        }
    }

startPassingBufferToFw:
    memset(buffer, 0x00, (pktlen + NBR_BYTES_ADD_TXFWINFO));

    /*
    ** we need to prepend the additional FW length indication plus
    ** the ADDR4 field. This is done by populating everything in a
    ** separate copybuffer and then finally pushed back into the 
    ** original skbuff...
    */
    memcpy(&buffer[OFFS_TXFWBUFF_IEEE80211HEADER], 
           &skb->data[OFFS_IEEE80211HEADER],
           NBR_BYTES_IEEE80211HEADER);

    memcpy(&buffer[OFFS_TXFWBUFF_IEEE80211PAYLOAD], 
           &skb->data[NBR_BYTES_IEEE80211HEADER], 
           pktlen - NBR_BYTES_IEEE80211COPYLEN);

    pFwLen = (u_int16_t *) skb_push(skb, NBR_BYTES_ADD_TXFWINFO);
    *pFwLen = ENDIAN_SWAP16(skb->len - NBR_BYTES_COMPLETE_TXFWHEADER);

    memcpy(&skb->data[NBR_BYTES_FW_TX_PREPEND_LEN],
           &buffer[OFFS_TXFWBUFF_IEEE80211HEADER],
           (skb->len - NBR_BYTES_FW_TX_PREPEND_LEN));

    mwlp->descData.pNextTxDesc->pSkBuff = skb;
    mwlp->descData.pNextTxDesc->PktLen = ENDIAN_SWAP16(skb->len);
    mwlp->descData.pNextTxDesc->pNode = ni; 
    if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
        mwlp->descData.pNextTxDesc->DataRate = (type) ? mwln->lastRxRate: 0;
        if (mwln->lastRxRateWasRateOfBmode) {
            mwlp->descData.pNextTxDesc->SapPktInfo = ENDIAN_SWAP32(0x01);
            mwlp->descData.pNextTxDesc->DataRate  &= 0xf;
        } else {
            mwlp->descData.pNextTxDesc->SapPktInfo = 0x00;
        }
        MWL_DBG_INFO(DBG_CLASS_RXTX, "use AP tx rate %i\n",
                     mwlp->descData.pNextTxDesc->DataRate);
    } else {
        mwlp->descData.pNextTxDesc->DataRate = (type) ? 1 : 0;
        MWL_DBG_INFO(DBG_CLASS_RXTX, "use STA tx rate %i\n",
                     mwlp->descData.pNextTxDesc->DataRate);
    }
    mwlp->descData.pNextTxDesc->PktPtr = 
        ENDIAN_SWAP32(pci_map_single(mwlp->pPciDev, skb->data, 
                                     skb->len, PCI_DMA_TODEVICE));
    mwlp->descData.pNextTxDesc->SoftStat |= SOFT_STAT_STALE;
    mwlp->descData.pNextTxDesc->Status = 
        ENDIAN_SWAP32(EAGLE_TXD_STATUS_USED | EAGLE_TXD_STATUS_FW_OWNED);
    mwlp->descData.pNextTxDesc = mwlp->descData.pNextTxDesc->pNext;

    writel(MACREG_H2ARIC_BIT_PPA_READY,
        mwlp->ioBase1 + MACREG_REG_H2A_INTERRUPT_EVENTS);
    dummy = readl(mwlp->ioBase1 + MACREG_REG_INT_CODE);

    netdev->trans_start = jiffies;
    MWL_DBG_EXIT(DBG_CLASS_RXTX);
    return MWL_OK;
}

static void
mwl_evaluateNewRssi(struct mwl_node *mwln, int newRssi)
{
    if (mwln->numRssiValues == 10) {
        mwln->numRssiValues = 1;
        mwln->sumRssi = newRssi;
        mwln->rssi = newRssi;
    } else {
        mwln->numRssiValues++;
        mwln->sumRssi = mwln->sumRssi + newRssi;
        mwln->rssi = mwln->sumRssi / mwln->numRssiValues;
    }
}

static void
mwl_evaluateNewRecvRate(struct ieee80211_node *ni, u_int8_t rxRate)
{
#define MRV_DATA_RATE_1MBIT_IDX      0
#define MRV_DATA_RATE_2MBIT_IDX      1
#define MRV_DATA_RATE_5_5MBIT_IDX    2
#define MRV_DATA_RATE_11MBIT_IDX     3
#define MRV_DATA_RATE_22MBIT_IDX     4
#define MRV_DATA_RATE_6MBIT_IDX      5
#define MRV_DATA_RATE_9MBIT_IDX      6
#define MRV_DATA_RATE_12MBIT_IDX     7
#define MRV_DATA_RATE_18MBIT_IDX     8
#define MRV_DATA_RATE_24MBIT_IDX     9
#define MRV_DATA_RATE_36MBIT_IDX     10
#define MRV_DATA_RATE_48MBIT_IDX     11
#define MRV_DATA_RATE_54MBIT_IDX     12
#define MRV_DATA_RATE_72MBIT_IDX     13
#define MRV_RATE_INDEX_MAX_ARRAY     14

    struct mwl_node *mwln = (struct mwl_node *) ni;
    u_int8_t phyRateTable[MRV_RATE_INDEX_MAX_ARRAY] =
        { 0x0A, 0x14, 0x37, 0x6E, 0xDC, 0x0B, 0x0F, 0x0A,
          0x0E, 0x09, 0x0D, 0x08, 0x0C, 0x07 };
    u_int8_t newRxRate = 0;
    int index;
    if (mwln == NULL) {
        return;
    }

    for(index = 0; index < MRV_RATE_INDEX_MAX_ARRAY; index++) {
        if(phyRateTable[index] == rxRate) {
            MWL_DBG_INFO(DBG_CLASS_RXTX, "matched rx rate %i to index %i",
                         rxRate, index);
            newRxRate = index;
            break;
        }
        if(index == (MRV_RATE_INDEX_MAX_ARRAY-1)) {
            newRxRate = MRV_DATA_RATE_2MBIT_IDX;
        }
    }

    mwln->lastRxRate = newRxRate;
    if ((mwln->lastRxRate == MRV_DATA_RATE_5_5MBIT_IDX) ||
        (mwln->lastRxRate == MRV_DATA_RATE_11MBIT_IDX)) {
        mwln->lastRxRateWasRateOfBmode = MWL_TRUE;
    } else {
        mwln->lastRxRateWasRateOfBmode = MWL_FALSE;
    }
}

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
