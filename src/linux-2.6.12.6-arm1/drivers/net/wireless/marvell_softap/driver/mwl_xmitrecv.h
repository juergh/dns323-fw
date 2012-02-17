/*******************************************************************************
 *
 * Name:        mwl_xmitrecv.h
 * Project:     Linux SoftAP for Marvell CB32/CB35 cardbus adapter
 * Version:     $Revision: 1.1.1.1 $
 * Date:        $Date: 2009/08/19 08:02:21 $
 * Author:      Ralph Roesler (rroesler@syskonnect.de)
 * Purpose:     transmit receive header file
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

#ifndef MWL_XMITRECV_H
#define MWL_XMITRECV_H

#include <linux/version.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/if_ether.h>   
#include <linux/if_arp.h>   

#include "mwl_descriptors.h"
#include "mwl_wlanext.h"

/*******************************************************************************
 *
 * Generic driver defines
 *
 ******************************************************************************/

extern int mwl_dataXmit(struct sk_buff *, struct net_device *);
extern int mwl_mgmtXmit(struct ieee80211com *, struct sk_buff *);
extern void mwl_xmitComplete(struct net_device *);
extern void mwl_recv(struct net_device *);
extern void mwl_mgmtRecv(struct ieee80211com *, struct sk_buff *, struct ieee80211_node *, int, int, u_int32_t);
extern void mwl_checkFwXmitBuffers(struct net_device *);


#define NBR_BYTES_FW_RX_PREPEND_LEN   2
#define NBR_BYTES_FW_TX_PREPEND_LEN   2
#define NBR_BYTES_CTRLSTATUS          2
#define NBR_BYTES_DURATION            2
#define NBR_BYTES_ADDR1               6
#define NBR_BYTES_ADDR2               6
#define NBR_BYTES_ADDR3               6
#define NBR_BYTES_SEQFRAG             2
#define NBR_BYTES_ADDR4               6
#define NBR_BYTES_TIMESTAMP           8
#define NBR_BYTES_BEACON_INTERVAL     2
#define NBR_BYTES_CAP_INFO            2
#define NBR_BYTES_FCS                 4

#define NBR_BYTES_ADD_RXFWINFO        ((NBR_BYTES_ADDR4) + \
                                       (NBR_BYTES_FW_RX_PREPEND_LEN))

#define NBR_BYTES_ADD_TXFWINFO        ((NBR_BYTES_ADDR4) + \
                                       (NBR_BYTES_FW_TX_PREPEND_LEN))

#define NBR_BYTES_COMPLETE_TXFWHEADER ((NBR_BYTES_FW_TX_PREPEND_LEN) + \
                                       (NBR_BYTES_CTRLSTATUS)        + \
                                       (NBR_BYTES_DURATION)          + \
                                       (NBR_BYTES_ADDR1)             + \
                                       (NBR_BYTES_ADDR2)             + \
                                       (NBR_BYTES_ADDR3)             + \
                                       (NBR_BYTES_SEQFRAG)           + \
                                       (NBR_BYTES_ADDR4))

#define NBR_BYTES_IEEE80211HEADER     ((NBR_BYTES_CTRLSTATUS) + \
                                       (NBR_BYTES_DURATION)   + \
                                       (NBR_BYTES_ADDR1)      + \
                                       (NBR_BYTES_ADDR2)      + \
                                       (NBR_BYTES_ADDR3)      + \
                                       (NBR_BYTES_SEQFRAG))

#define NBR_BYTES_IEEE80211COPYLEN    ((NBR_BYTES_IEEE80211HEADER) - \
                                       (NBR_BYTES_ADDR4))

#define OFFS_IEEE80211HEADER           0
#define OFFS_IEEE80211PAYLOAD          (NBR_BYTES_IEEE80211HEADER)
#define OFFS_TXFWBUFF_IEEE80211HEADER  (NBR_BYTES_FW_TX_PREPEND_LEN)
#define OFFS_TXFWBUFF_IEEE80211PAYLOAD (NBR_BYTES_COMPLETE_TXFWHEADER)
#define OFFS_RXFWBUFF_IEEE80211HEADER  (NBR_BYTES_FW_TX_PREPEND_LEN)
#define OFFS_RXFWBUFF_IEEE80211PAYLOAD (NBR_BYTES_COMPLETE_TXFWHEADER)

#endif /* MWL_XMITRECV_H */

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
