/*******************************************************************************
 *
 * Name:        mwl_download.c
 * Project:     Linux SoftAP for Marvell CB32/CB35 cardbus adapter
 * Version:     $Revision: 1.1.1.1 $
 * Date:        $Date: 2009/08/19 08:02:21 $
 * Author:      Ralph Roesler (rroesler@syskonnect.de)
 * Purpose:     download functions
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

#include "img_cb35_fw_duplex.h"
#include "mwl_version.h"
#include "mwl_download.h"
#include "mwl_registers.h"
#include "mwl_hostcmd.h"
#include "mwl_main.h"

/*******************************************************************************
 *
 * Definitions (defines, typedefs, prototypes etc.)
 *
 ******************************************************************************/

#define FW_DOWNLOAD_BLOCK_SIZE                 256  /* FW fraglen to download */
#define FW_CHECK_MSECS                           5  /* wait 5 msecs each check*/
#define FW_MAX_NUM_CHECKS                      200  /* check FW max 200 times */
#define FW_LOAD_STA_FWRDY_SIGNATURE     0xf0f1f2f4
#define FW_LOAD_SOFTAP_FWRDY_SIGNATURE  0xf1f2f4a5

/*******************************************************************************
 *
 *          Define OpMode for SoftAP/Station mode
 *
 *  The following mode signature has to be written to PCI scratch register#0
 *  right after successfully downloading the last block of firmware and
 *  before waiting for firmware ready signature
 *
 ******************************************************************************/

#define HostCmd_STA_MODE     0x5A
#define HostCmd_SOFTAP_MODE  0xA5

#ifdef MWL_KERNEL_26
#ifndef FEROCEON
#define MWL_SEC_SLEEP(NumSecs)       msleep(NumSecs * 1000); 
#define MWL_MSEC_SLEEP(NumMilliSecs) msleep(NumMilliSecs);
#else
#define MWL_SEC_SLEEP(NumSecs)       mdelay(NumSecs * 1000); 
#define MWL_MSEC_SLEEP(NumMilliSecs) mdelay(NumMilliSecs);
#endif
#else 
#define MWL_SEC_SLEEP(NumSecs)       mdelay(NumSecs * 1000); 
#define MWL_MSEC_SLEEP(NumMilliSecs) mdelay(NumMilliSecs);
#endif

static void mwl_triggerPciCmd(struct net_device *);

/*******************************************************************************
 *
 * Global functions
 *
 ******************************************************************************/

int
mwl_downloadFirmware(struct net_device *netdev, enum ieee80211_opmode opmode)
{
    struct mwl_private *mwlp = netdev->priv;
    unsigned char *pFwImage = &fmimage_duplex[0];
    unsigned int currIteration = FW_MAX_NUM_CHECKS;
    unsigned short firmwareBlockSize = FW_DOWNLOAD_BLOCK_SIZE;
    unsigned int FwReadySignature = FW_LOAD_STA_FWRDY_SIGNATURE; 
    unsigned int OpMode = HostCmd_STA_MODE; 
    unsigned int downloadSuccessful = 1;
    unsigned int sizeFwDownloaded = 0;
    unsigned int remainingFwBytes = 0;
    unsigned int intCode;

    MWL_DBG_ENTER(DBG_CLASS_DNLD);

    /*
    ** make sure that any previously loaded FW is thrown away...
    */
    mwl_resetAdapterAndVanishFw(netdev);

    writel(MACREG_A2HRIC_BIT_MASK, mwlp->ioBase1 + MACREG_REG_A2H_INTERRUPT_CLEAR_SEL);
    writel(0x00,mwlp->ioBase1+MACREG_REG_A2H_INTERRUPT_CAUSE);
    writel(0x00,mwlp->ioBase1+MACREG_REG_A2H_INTERRUPT_MASK);
    writel(MACREG_A2HRIC_BIT_MASK, mwlp->ioBase1 + MACREG_REG_A2H_INTERRUPT_STATUS_MASK);

    /*
    ** initialize opmode and ready signature depending on desired download
    */
    if (opmode == IEEE80211_M_HOSTAP) {
        FwReadySignature = FW_LOAD_SOFTAP_FWRDY_SIGNATURE;
        OpMode = HostCmd_SOFTAP_MODE;
    }

    MWL_DBG_INFO(DBG_CLASS_DNLD, "download %s fw (%i bytes)",
        (opmode == IEEE80211_M_HOSTAP) ? "AP" : "Station",
        sizeof(fmimage_duplex));

    /* 
    ** download the firmware itself: note that the firmware 
    ** needs to be transferred in a sequence of blocks
    */
    while (sizeFwDownloaded < sizeof(fmimage_duplex)) {
        remainingFwBytes = sizeof(fmimage_duplex) - sizeFwDownloaded;
        if (remainingFwBytes < FW_DOWNLOAD_BLOCK_SIZE) {
            firmwareBlockSize = remainingFwBytes;
        }
        /* using two writew's iso this writel did not work on an ixp422 system */
        writel(((FW_DOWNLOAD_BLOCK_SIZE << 16) | HostCmd_CMD_CODE_DNLD), mwlp->pCmdBuf);
        memcpy(&mwlp->pCmdBuf[4],(pFwImage+sizeFwDownloaded),firmwareBlockSize); 
        mwl_triggerPciCmd(netdev);

        currIteration = FW_MAX_NUM_CHECKS;
        do {
            currIteration--;
            MWL_MSEC_SLEEP(FW_CHECK_MSECS);
            intCode = readl(mwlp->ioBase1 + MACREG_REG_INT_CODE);
        } while ((currIteration) && (intCode != MACREG_INT_CODE_CMD_FINISHED));

        if (currIteration == 0) {
            downloadSuccessful = 0;
            mwl_resetAdapterAndVanishFw(netdev);
            printk(KERN_ERR "%s: %s fw download timeout\n", DRV_NAME,
                   (opmode == IEEE80211_M_HOSTAP) ? "AP" : "Station");
            return MWL_DNLD_FWIMG_FAILED;
        }

        writel(0x00, mwlp->ioBase1 + MACREG_REG_INT_CODE); 
        sizeFwDownloaded += (unsigned int) firmwareBlockSize;
    }

    /* 
    ** send a command with size 0 to tell firmware we 
    ** transferred all bytes and finished downloading
    */
    if (downloadSuccessful) {
        writew(0x00, &mwlp->pCmdBuf[1]);
        mwl_triggerPciCmd(netdev);
        currIteration = FW_MAX_NUM_CHECKS;
        do {
            currIteration--;
            writel(OpMode, mwlp->ioBase1 + MACREG_REG_GEN_PTR);
            MWL_MSEC_SLEEP(FW_CHECK_MSECS);
            intCode = readl(mwlp->ioBase1 + MACREG_REG_INT_CODE);
        } while ((currIteration) && (intCode != FwReadySignature));

        if (currIteration == 0) {
            downloadSuccessful = 0;
            mwl_resetAdapterAndVanishFw(netdev);
            printk(KERN_ERR "%s: %s fw start timeout\n", DRV_NAME,
                   (opmode == IEEE80211_M_HOSTAP) ? "AP" : "Station");
            return MWL_TIMEOUT;
        } 
    }

    writel(0x00, mwlp->ioBase1 + MACREG_REG_INT_CODE); /* clear IRQ */
    MWL_DBG_EXIT(DBG_CLASS_DNLD);
    return MWL_OK;
}

/*******************************************************************************
 *
 * Local functions
 *
 ******************************************************************************/

static void 
mwl_triggerPciCmd(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    unsigned long dummy;

    writel(mwlp->pPhysCmdBuf, mwlp->ioBase1 + MACREG_REG_GEN_PTR);
    dummy = readl(mwlp->ioBase1 + MACREG_REG_INT_CODE);

    writel(0x00, mwlp->ioBase1 + MACREG_REG_INT_CODE); /* clear IRQ */
    dummy = readl(mwlp->ioBase1 + MACREG_REG_INT_CODE);

    writel(MACREG_H2ARIC_BIT_DOOR_BELL, 
            mwlp->ioBase1 + MACREG_REG_H2A_INTERRUPT_EVENTS);
    dummy = readl(mwlp->ioBase1 + MACREG_REG_INT_CODE);
}

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
