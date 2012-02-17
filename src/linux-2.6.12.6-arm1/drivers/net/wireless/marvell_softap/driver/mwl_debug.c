/*******************************************************************************
 *
 * Name:        mwl_debug.c
 * Project:     Linux SoftAP for Marvell CB32/CB35 cardbus adapter
 * Version:     $Revision: 1.1.1.1 $
 * Date:        $Date: 2009/08/19 08:02:21 $
 * Author:      Ralph Roesler (rroesler@syskonnect.de)
 * Purpose:     debug functions
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
#include "mwl_main.h"
#include <stdarg.h>

/*******************************************************************************
 *
 * Definitions (defines, typedefs, prototypes etc.)
 *
 ******************************************************************************/

#define TRIM_STRING_NEWLINE(trimstr)          \
    if (trimstr[strlen(trimstr)-1] == '\n') { \
        trimstr[strlen(trimstr)-1] = '\0';    \
    }

#define MAX_LEN_DBG_STRING 1024
#define MAX_LEN_DBG_DATA      8

#ifndef MWL_DBG_TYPES
#define MWL_DBG_TYPES    (DBG_TYPE_ENTER   | \
                          DBG_TYPE_EXIT    | \
                          DBG_TYPE_MSG     | \
                          DBG_TYPE_DATA)
#endif

#ifndef MWL_DBG_LVL
#define MWL_DBG_LVL      (DBG_LVL_INFO     | \
                          DBG_LVL_WARNING  | \
                          DBG_LVL_ERROR)
#endif

#ifndef MWL_DBG_CLASSES
#define MWL_DBG_CLASSES  (DBG_CLASS_DESC   | \
                          DBG_CLASS_DNLD   | \
                          DBG_CLASS_ETHT   | \
                          DBG_CLASS_HCMD   | \
                          DBG_CLASS_HPLUG  | \
                          DBG_CLASS_MAIN   | \
                          DBG_CLASS_WLEXT  | \
                          DBG_CLASS_RXTX)
#endif

/*******************************************************************************
 *
 * Global functions
 *
 ******************************************************************************/

void 
mwl_dbgMsg(const char *file, const char *func, int line, u_int32_t type, u_int32_t classlevel, const char *format, ... ) 
{
    unsigned char debugString[MAX_LEN_DBG_STRING] = "";
    u_int32_t level = classlevel & 0x0000ffff;
    u_int32_t class = classlevel & 0xffff0000;
#ifdef MWL_DEBUG_VERBOSE
    int verboseLog = 1;
#else
    int verboseLog = 0;
#endif
    va_list a_start;


    if ((class & MWL_DBG_CLASSES) != class) {
        return; /* debug class not allowed */
    }

    if ((level & MWL_DBG_LVL) != level) {
        return; /* debug level not allowed */
    }

    if ((type & MWL_DBG_TYPES) != type) {
        return; /* debug type not allowed */
    }
 
    if (format != NULL) {
        va_start(a_start, format); 
        vsprintf(debugString, format, a_start);
        va_end(a_start);
    }

    switch (level) {
        case DBG_LVL_INFO:
            if (type == DBG_TYPE_ENTER) {
                printk(KERN_INFO "%s(): Enter function...\n", func);
                if (strlen(debugString) > 0) {
                    TRIM_STRING_NEWLINE(debugString);
                    printk(KERN_INFO "%s() %s (line %i)\n", 
                                func, debugString, line);
                }
            } else if (type == DBG_TYPE_EXIT) {
                if (strlen(debugString) > 0) {
                    TRIM_STRING_NEWLINE(debugString);
                    printk(KERN_INFO "%s() %s (line %i)\n", 
                                func, debugString, line);
                }
                printk(KERN_INFO "%s(): ...exit function\n", func);
            } else {
                if (strlen(debugString) > 0) {
                    TRIM_STRING_NEWLINE(debugString);
                    if (verboseLog) {
                        printk(KERN_INFO "%s() %s (line %i)\n", 
                                    func, debugString, line);
                    } else {
                        printk(KERN_INFO "%s\n", debugString);
                    }
                }
            }
            break;
        case DBG_LVL_WARNING:
            if (type == DBG_TYPE_ENTER) {
                printk(KERN_WARNING "%s(): (WARN) Enter function...\n", func);
                if (strlen(debugString) > 0) {
                    TRIM_STRING_NEWLINE(debugString);
                    printk(KERN_WARNING "%s() (WARN) %s (line %i)\n", 
                                func, debugString, line);
                }

            } else if (type == DBG_TYPE_EXIT) {
                if (strlen(debugString) > 0) {
                    TRIM_STRING_NEWLINE(debugString);
                    printk(KERN_WARNING "%s() (WARN) %s (line %i)\n", 
                                func, debugString, line);
                }
                printk(KERN_WARNING "%s(): (WARN) ...exit function\n", func);
            } else {
                if (strlen(debugString) > 0) {
                    TRIM_STRING_NEWLINE(debugString);
                    if (verboseLog) {
                        printk(KERN_WARNING "%s() (WARN) %s (line %i)\n", 
                                    func, debugString, line);
                    } else {
                        printk(KERN_WARNING "(WARN) %s\n", debugString);
                    }
                }
            }
            break;
        case DBG_LVL_ERROR:
            if (type == DBG_TYPE_ENTER) {
                printk(KERN_ERR "%s(): (ERR) Enter function...\n", func);
                if (strlen(debugString) > 0) {
                    TRIM_STRING_NEWLINE(debugString);
                    printk(KERN_ERR "%s() (ERR) %s (line %i)\n", 
                                func, debugString, line);
                }
            } else if (type == DBG_TYPE_EXIT) {
                if (strlen(debugString) > 0) {
                    TRIM_STRING_NEWLINE(debugString);
                    printk(KERN_ERR "%s() (ERR) %s (line %i)\n", 
                                func, debugString, line);
                }
                printk(KERN_ERR "%s(): (ERR) ...exit function\n", func);
            } else {
                if (strlen(debugString) > 0) {
                    TRIM_STRING_NEWLINE(debugString);
                    if (verboseLog) {
                        printk(KERN_ERR "%s() (ERR) %s (line %i)\n", 
                                    func, debugString, line);
                    } else {
                        printk(KERN_ERR "(ERR) %s\n", debugString);
                    }
                }
            }
            break;
        default:
            printk(KERN_ERR "unknown level of dbgmsg (%i)\n", level);
            break;
    }
}

void 
mwl_dbgData(const char *file, const char *func, int line, u_int32_t type, u_int32_t classlevel, const void *data, int len, const char *format, ... )
{
    unsigned char debugString[MAX_LEN_DBG_STRING] = "";
    unsigned char debugData[MAX_LEN_DBG_DATA] = "";
    unsigned char *memptr = (unsigned char *) data;
    u_int32_t level = classlevel & 0x0000ffff;
    u_int32_t class = classlevel & 0xffff0000;
#ifdef MWL_DEBUG_VERBOSE
    int verboseLog = 1;
#else
    int verboseLog = 0;
#endif
    int currByte = 0;
    int numBytes = 0;
    int offset = 0;
    va_list a_start;

    if ((class & MWL_DBG_CLASSES) != class) {
        return; /* debug class not allowed */
    }

    if ((level & MWL_DBG_LVL) != level) {
        return; /* debug level not allowed */
    }

    if ((type & MWL_DBG_TYPES) != type) {
        return; /* debug type not allowed */
    }
 
    if (format != NULL) {
        va_start(a_start, format); 
        vsprintf(debugString, format, a_start);
        va_end(a_start);
    }

    if (level == DBG_LVL_INFO) {
        if (strlen(debugString) > 0) {
            TRIM_STRING_NEWLINE(debugString);
            if (verboseLog) {
                printk(KERN_INFO "%s() %s (line %i)\n", 
                            func, debugString, line);
            } else {
                printk(KERN_INFO "%s\n", debugString);
            }
        }
        for (currByte = 0; currByte < len; currByte=currByte+8) {
            if ((currByte + 8) < len) {
                printk(KERN_INFO "%s() 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n", 
                func,
                * (memptr + currByte + 0),
                * (memptr + currByte + 1),
                * (memptr + currByte + 2),
                * (memptr + currByte + 3),
                * (memptr + currByte + 4),
                * (memptr + currByte + 5),
                * (memptr + currByte + 6),
                * (memptr + currByte + 7));
             } else {
                 numBytes = len - currByte;
                 offset = currByte;
                 sprintf(debugString, "%s() ", func);
                 for (currByte = 0; currByte < numBytes; currByte++ ) {
                     sprintf(debugData, "0x%02x ", * (memptr + offset + currByte));
                     strcat(debugString, debugData);
                 }
                 printk(KERN_INFO "%s\n", debugString);
                 break; /* get out of for-loop */
             }
        }
    } else {
        printk(KERN_ERR "unknown level of dbgmsg (%i)\n", level);
    }
}

int 
mwl_logFrame(struct net_device *netdev, struct sk_buff *skb)
{
    int currByte = 0;
    int numBytes = 0;
    int offset = 0;

    for (currByte = 0; currByte < skb->len; currByte=currByte+8) {
        if ((currByte + 8) < skb->len) {
            printk("0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n", 
                skb->data[currByte + 0],
                skb->data[currByte + 1],
                skb->data[currByte + 2],
                skb->data[currByte + 3],
                skb->data[currByte + 4],
                skb->data[currByte + 5],
                skb->data[currByte + 6],
                skb->data[currByte + 7]);
         } else {
             numBytes = skb->len - currByte;
             offset = currByte;
             for (currByte = 0; currByte < numBytes; currByte++ ) {
                 printk("0x%02x ", skb->data[offset + currByte]);
             }
             printk("\n");
             break; /* get out of for-loop */
         }
    }
    return MWL_OK;
}

int 
mwl_logTxDesc(struct net_device *netdev, mwl_txdesc_t *desc)
{
    printk("desc-addr (virt) 0x%p\n", desc);
    printk("desc-skbuff  0x%p\n", desc->pSkBuff);
    if (desc->pSkBuff != NULL) {
        printk("desc-len %i, sk_buff-len: %i\n", desc->PktLen, desc->pSkBuff->len);
        printk("sk_buff virt address: 0x%p\n", desc->pSkBuff);
        printk("sk_buff->data phys address: 0x%x\n", desc->PktPtr);
        printk("sk_buff->data virt address: 0x%p\n", desc->pSkBuff->data);
    }
    printk("desc->DataRate is: %i (0x%0x)\n", desc->DataRate, desc->DataRate);
    printk("desc->SoftStat is: %i (0x%0x)\n", desc->SoftStat, desc->SoftStat);
    printk("desc->Status is: %i (0x%0x)\n", desc->Status, desc->Status);
    printk("next desc-addr (virt) 0x%p\n", desc->pNext);

    return MWL_OK;
}

int 
mwl_dumpStatistics(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    
    printk("txRetrySuccesses   : %i\n", mwlp->privStats.txRetrySuccesses);
    printk("txMultiRetrySucc   : %i\n", mwlp->privStats.txMultRetrySucc);
    printk("txFailures         : %i\n", mwlp->privStats.txFailures);
    printk("RTSSuccesses       : %i\n", mwlp->privStats.RTSSuccesses);
    printk("RTSFailures        : %i\n", mwlp->privStats.RTSFailures);
    printk("ackFailures        : %i\n", mwlp->privStats.ackFailures);
    printk("rxDuplicateFrames  : %i\n", mwlp->privStats.rxDuplicateFrames);
    printk("FCSErrorCount      : %i\n", mwlp->privStats.FCSErrorCount);
    printk("txWatchDogTimeouts : %i\n", mwlp->privStats.txWatchDogTimeouts);
    printk("rxOverflows        : %i\n", mwlp->privStats.rxOverflows);
    printk("rxFragErrors       : %i\n", mwlp->privStats.rxFragErrors);
    printk("rxMemErrors        : %i\n", mwlp->privStats.rxMemErrors);
    printk("pointerErrors      : %i\n", mwlp->privStats.pointerErrors);
    printk("txUnderflows       : %i\n", mwlp->privStats.txUnderflows);
    printk("txDone             : %i\n", mwlp->privStats.txDone);
    printk("txDoneBufTryPut    : %i\n", mwlp->privStats.txDoneBufTryPut);
    printk("txDoneBufPut       : %i\n", mwlp->privStats.txDoneBufPut);
    printk("wait4TxBuf         : %i\n", mwlp->privStats.wait4TxBuf);
    printk("txAttempts         : %i\n", mwlp->privStats.txAttempts);
    printk("txSuccesses        : %i\n", mwlp->privStats.txSuccesses);
    printk("txFragments        : %i\n", mwlp->privStats.txFragments);
    printk("txMulticasts       : %i\n", mwlp->privStats.txMulticasts);
    printk("rxNonCtlPkts       : %i\n", mwlp->privStats.rxNonCtlPkts);
    printk("rxMulticasts       : %i\n", mwlp->privStats.rxMulticasts);
    printk("rxUndecryptFrames  : %i\n", mwlp->privStats.rxUndecryptFrames);
    printk("rxICVErrors        : %i\n", mwlp->privStats.rxICVErrors);
    printk("rxExcludedFrames   : %i\n", mwlp->privStats.rxExcludedFrames);
    return MWL_OK;
}

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
