/*******************************************************************************
 *
 * Name:        mwl_wlanext.h
 * Project:     Linux SoftAP for Marvell CB32/CB35 cardbus adapter
 * Version:     $Revision: 1.1.1.1 $
 * Date:        $Date: 2009/08/19 08:02:21 $
 * Author:      Ralph Roesler (rroesler@syskonnect.de)
 * Purpose:     WLAN extensions header file
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

#ifndef MWL_WLANEXT_H
#define MWL_WLANEXT_H

#include <linux/types.h>
#include <linux/if.h>
#include <linux/ethtool.h>
#include <linux/version.h>
#include <linux/if_ether.h>   
#include <linux/string.h>
#include <net/iw_handler.h>

#include "if_ethersubr.h"
#include "if_media.h"
#include "if_llc.h"
#include "ieee80211_var.h"

/*******************************************************************************
 *
 * Generic driver defines
 *
 ******************************************************************************/

extern int mwl_setWlanExtOps(struct net_device *netdev);

#endif /* MWL_WLANEXT_H */

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
