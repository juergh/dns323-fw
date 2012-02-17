/*******************************************************************************
 *
 * Name:        mwl_debug.h
 * Project:     Linux SoftAP for Marvell CB32/CB35 cardbus adapter
 * Version:     $Revision: 1.1.1.1 $
 * Date:        $Date: 2009/08/19 08:02:21 $
 * Author:      Ralph Roesler (rroesler@syskonnect.de)
 * Purpose:     debug header file
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

#ifndef MWL_DEBUG_H
#define MWL_DEBUG_H

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

#include "mwl_main.h"

/*******************************************************************************
 *
 * Generic driver defines
 *
 ******************************************************************************/

#define DBG_TYPE_ENTER    0x00000001
#define DBG_TYPE_EXIT     0x00000002
#define DBG_TYPE_MSG      0x00000004
#define DBG_TYPE_DATA     0x00000008

#define DBG_LVL_INFO      0x00000001
#define DBG_LVL_WARNING   0x00000002
#define DBG_LVL_ERROR     0x00000004

#define DBG_CLASS_DESC    0x80000000
#define DBG_CLASS_DNLD    0x40000000
#define DBG_CLASS_ETHT    0x20000000
#define DBG_CLASS_HCMD    0x10000000
#define DBG_CLASS_HPLUG   0x08000000
#define DBG_CLASS_MAIN    0x04000000
#define DBG_CLASS_WLEXT   0x02000000
#define DBG_CLASS_RXTX    0x01000000

extern int mwl_logFrame(struct net_device *, struct sk_buff *);
extern int mwl_logTxDesc(struct net_device *, mwl_txdesc_t *);
extern int mwl_dumpStatistics(struct net_device *netdev);

#ifdef MWL_DEBUG
extern void mwl_dbgData(const char *file, const char *func, int line, u_int32_t type, u_int32_t classlevel, const void *data, int len, const char *format, ... );

#define MWL_DBG_DATA(classlevel, data, len)                                \
    mwl_dbgData(__FILE__, __FUNCTION__, __LINE__,                          \
                DBG_TYPE_DATA, (classlevel|DBG_LVL_INFO),                  \
                data, len, NULL)

extern void mwl_dbgMsg(const char *file, const char *func, int line, u_int32_t type, u_int32_t classlevel, const char *format, ... );

#define MWL_DBG_ENTER(classlevel)                                          \
    mwl_dbgMsg(__FILE__, __FUNCTION__, __LINE__,                           \
               DBG_TYPE_ENTER, (classlevel|DBG_LVL_INFO), NULL)

#define MWL_DBG_ENTER_INFO(classlevel, ... )                               \
    mwl_dbgMsg(__FILE__, __FUNCTION__, __LINE__,                           \
               DBG_TYPE_ENTER, (classlevel|DBG_LVL_INFO), NULL);           \
    mwl_dbgMsg(__FILE__, __FUNCTION__, __LINE__,                           \
               DBG_TYPE_MSG ,(classlevel|DBG_LVL_INFO), __VA_ARGS__ )

#define MWL_DBG_EXIT(classlevel)                                           \
    mwl_dbgMsg(__FILE__, __FUNCTION__, __LINE__,                           \
               DBG_TYPE_EXIT , (classlevel|DBG_LVL_INFO), NULL )

#define MWL_DBG_EXIT_INFO(classlevel, ... )                                \
    mwl_dbgMsg(__FILE__, __FUNCTION__, __LINE__,                           \
               DBG_TYPE_MSG , (classlevel|DBG_LVL_INFO), __VA_ARGS__ );    \
    mwl_dbgMsg(__FILE__, __FUNCTION__, __LINE__,                           \
               DBG_TYPE_EXIT , (classlevel|DBG_LVL_INFO), NULL )

#define MWL_DBG_EXIT_WARNING(classlevel, ... )                             \
    mwl_dbgMsg(__FILE__, __FUNCTION__, __LINE__,                           \
               DBG_TYPE_MSG , (classlevel|DBG_LVL_WARNING), __VA_ARGS__ ); \
    mwl_dbgMsg(__FILE__, __FUNCTION__, __LINE__,                           \
               DBG_TYPE_EXIT , (classlevel|DBG_LVL_INFO), NULL )

#define MWL_DBG_EXIT_ERROR(classlevel, ... )                               \
    mwl_dbgMsg(__FILE__, __FUNCTION__, __LINE__,                           \
               DBG_TYPE_MSG , (classlevel|DBG_LVL_ERROR), __VA_ARGS__ );   \
    mwl_dbgMsg(__FILE__, __FUNCTION__, __LINE__,                           \
               DBG_TYPE_EXIT , (classlevel|DBG_LVL_INFO), NULL )

#define MWL_DBG_EXIT_DATA(classlevel, data, len)                           \
    mwl_dbgData(__FILE__, __FUNCTION__, __LINE__,                          \
                DBG_TYPE_DATA, (classlevel|DBG_LVL_INFO), data, len, NULL);\
    mwl_dbgMsg(__FILE__, __FUNCTION__, __LINE__,                           \
               DBG_TYPE_EXIT , (classlevel|DBG_LVL_INFO), NULL )

#define MWL_DBG_EXIT_INFO_DATA(classlevel, data, len, ... )                \
    mwl_dbgMsg(__FILE__, __FUNCTION__, __LINE__,                           \
               DBG_TYPE_MSG , (classlevel|DBG_LVL_INFO), __VA_ARGS__ );    \
    mwl_dbgData(__FILE__, __FUNCTION__, __LINE__,                          \
                DBG_TYPE_DATA, (classlevel|DBG_LVL_INFO), data, len, NULL);\
    mwl_dbgMsg(__FILE__, __FUNCTION__, __LINE__,                           \
               DBG_TYPE_EXIT , (classlevel|DBG_LVL_INFO), NULL )

#define MWL_DBG_INFO(classlevel, ... )                                     \
    mwl_dbgMsg(__FILE__, __FUNCTION__, __LINE__,                           \
               DBG_TYPE_MSG , (classlevel|DBG_LVL_INFO), __VA_ARGS__ )

#define MWL_DBG_WARN(classlevel, ... )                                     \
    mwl_dbgMsg(__FILE__, __FUNCTION__, __LINE__,                           \
               DBG_TYPE_MSG , (classlevel|DBG_LVL_WARNING), __VA_ARGS__ )

#define MWL_DBG_ERROR(classlevel, ... )                                    \
    mwl_dbgMsg(__FILE__, __FUNCTION__, __LINE__,                           \
               DBG_TYPE_MSG , (classlevel|DBG_LVL_ERROR), __VA_ARGS__ )

#define MWL_DBG_MSG(type, classlevel, ... )                                \
    mwl_dbgMsg(__FILE__, __FUNCTION__, __LINE__,                           \
               type, classlevel, __VA_ARGS__ )

#else

#define MWL_DBG_DATA(classlevel, data, len)
#define MWL_DBG_ENTER(classlevel)
#define MWL_DBG_ENTER_INFO(classlevel, ... ) 
#define MWL_DBG_EXIT(classlevel)
#define MWL_DBG_EXIT_INFO(classlevel, ... ) 
#define MWL_DBG_EXIT_WARNING(classlevel, ... )
#define MWL_DBG_EXIT_ERROR(classlevel, ... )
#define MWL_DBG_EXIT_DATA(classlevel, data, len)
#define MWL_DBG_EXIT_INFO_DATA(classlevel, data, len, ... )
#define MWL_DBG_INFO(classlevel, ... )
#define MWL_DBG_WARN(classlevel, ... )
#define MWL_DBG_ERROR(classlevel, ... )

#endif /* MWL_DEBUG */

#endif /* MWL_DEBUG_H */

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
