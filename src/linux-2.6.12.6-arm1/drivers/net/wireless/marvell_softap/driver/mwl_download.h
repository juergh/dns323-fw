/*******************************************************************************
 *
 * Name:        mwl_download.h
 * Project:     Linux SoftAP for Marvell CB32/CB35 cardbus adapter
 * Version:     $Revision: 1.1.1.1 $
 * Date:        $Date: 2009/08/19 08:02:21 $
 * Author:      Ralph Roesler (rroesler@syskonnect.de)
 * Purpose:     download header file
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

#ifndef MWL_DOWNLOAD_H
#define MWL_DOWNLOAD_H

#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/version.h>
#include "if_media.h"
#include "ieee80211.h"
#include "ieee80211_crypto.h"
#include "ieee80211_var.h"

/*******************************************************************************
 *
 * Generic driver defines
 *
 ******************************************************************************/

extern int mwl_downloadFirmware(struct net_device *, enum ieee80211_opmode);

#endif /* MWL_DOWNLOAD_H */

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
