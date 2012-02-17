/*******************************************************************************
 *
 * Name:        mwl_ethtool.h
 * Project:     Linux SoftAP for Marvell CB32/CB35 cardbus adapter
 * Version:     $Revision: 1.1.1.1 $
 * Date:        $Date: 2009/08/19 08:02:21 $
 * Author:      Ralph Roesler (rroesler@syskonnect.de)
 * Purpose:     ethtool header file
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

#ifndef MWL_ETHTOOL_H
#define MWL_ETHTOOL_H

#include <linux/types.h>
#include <linux/if.h>
#include <linux/ethtool.h>
#include <linux/version.h>
#include <linux/netdevice.h>
#include <asm/uaccess.h>
#include "if_media.h"
#include "ieee80211.h"
#include "ieee80211_crypto.h"
#include "ieee80211_var.h"

/*******************************************************************************
 *
 * Generic driver defines
 *
 ******************************************************************************/

extern int mwl_setEthtoolOps(struct net_device *netdev);
extern int mwl_doEthtoolIoctl(struct ieee80211com *, struct ifreq *, int);

#endif /* MWL_ETHTOOL_H */

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
