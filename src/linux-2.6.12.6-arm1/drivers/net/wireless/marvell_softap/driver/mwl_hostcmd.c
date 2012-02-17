/*******************************************************************************
 *
 * Name:        mwl_hostcmd.c
 * Project:     Linux SoftAP for Marvell CB32/CB35 cardbus adapter
 * Version:     $Revision: 1.1.1.1 $
 * Date:        $Date: 2009/08/19 08:02:21 $
 * Author:      Ralph Roesler (rroesler@syskonnect.de)
 * Purpose:     host command functions
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

#include "mwl_registers.h"
#include "mwl_hostcmd.h"
#include "mwl_xmitrecv.h"
#include "mwl_main.h"
#include "mwl_version.h"

/*******************************************************************************
 *
 * Definitions (defines, typedefs, prototypes etc.)
 *
 ******************************************************************************/

static int mwl_executeCommand(struct net_device *, unsigned short);
static void mwl_sendCommand(struct net_device *);
static int mwl_waitForComplete(struct net_device *, u_int16_t);
static int mwl_getIBSSoff(u_int8_t *, u_int8_t);
static int mwl_getWPAoff(u_int8_t *, u_int8_t);
static char *mwl_getCmdString(u_int16_t cmd);
static char *mwl_getCmdResultString(u_int16_t result);
static char *mwl_getDrvName(struct net_device *netdev);
static int addStaEncrKey(const u_int8_t mac[IEEE80211_ADDR_LEN], const struct ieee80211_key *);
static u_int8_t *delStaEncrKey(const struct ieee80211_key *);

#define MAX_WAIT_FW_COMPLETE_ITERATIONS 10000

struct mwl_stamac_encrkey {
    u_int8_t mac[IEEE80211_ADDR_LEN];      /* MAC address of the peer station */
    u_int8_t key[IEEE80211_KEYBUF_SIZE+IEEE80211_MICBUF_SIZE];   /* encr. key */
};

#define MAX_NUM_STA_ENCR_KEY_ENTRIES 128
static struct mwl_stamac_encrkey staEncrKeyMap[MAX_NUM_STA_ENCR_KEY_ENTRIES];
static u_int32_t numStaEncrKeyEntries = 0;

/*******************************************************************************
 *
 * Global functions
 *
 ******************************************************************************/

void
mwl_fwCommandComplete(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    u_int16_t cmd = ENDIAN_SWAP16(((FWCmdHdr*)mwlp->pCmdBuf)->Cmd) & 0x7fff;
    u_int16_t result =ENDIAN_SWAP16(((FWCmdHdr*)mwlp->pCmdBuf)->Result);

    if (result != HostCmd_RESULT_OK) {
        printk(KERN_ERR "%s: FW cmd 0x%04x=%s failed: 0x%04x=%s\n",
               mwl_getDrvName(netdev), cmd, mwl_getCmdString(cmd),
               result, mwl_getCmdResultString(result));
    }
}

int
mwl_getFwHardwareSpecs(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_DS_GET_HW_SPEC *pCmd = (HostCmd_DS_GET_HW_SPEC *)&mwlp->pCmdBuf[0];

    MWL_DBG_ENTER(DBG_CLASS_HCMD);

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_DS_GET_HW_SPEC));
    memset(&pCmd->PermanentAddr[0], 0xff, ETH_ALEN);
    pCmd->CmdHdr.Cmd      = ENDIAN_SWAP16(HostCmd_CMD_GET_HW_SPEC);
    pCmd->CmdHdr.Length   = ENDIAN_SWAP16(sizeof(HostCmd_DS_GET_HW_SPEC));
    pCmd->ulFwAwakeCookie = ENDIAN_SWAP32((unsigned int)mwlp->pPhysCmdBuf+2048);

    MWL_DBG_DATA(DBG_CLASS_HCMD, (void *) pCmd, sizeof(HostCmd_DS_GET_HW_SPEC));
    if (mwl_executeCommand(netdev, HostCmd_CMD_GET_HW_SPEC)) {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_HCMD, "failed execution");
        spin_unlock(&mwlp->locks.fwLock);
        return MWL_ERROR;
    }

    memcpy(&mwlp->hwData.macAddr[0],pCmd->PermanentAddr,ETH_ALEN);
    mwlp->descData.wcbBase       = ENDIAN_SWAP32(pCmd->WcbBase0)   & 0x0000ffff;
    mwlp->descData.rxDescRead    = ENDIAN_SWAP32(pCmd->RxPdRdPtr)  & 0x0000ffff;
    mwlp->descData.rxDescWrite   = ENDIAN_SWAP32(pCmd->RxPdWrPtr)  & 0x0000ffff;
    mwlp->hwData.regionCode      = ENDIAN_SWAP16(pCmd->RegionCode) & 0x00ff;
    mwlp->hwData.fwReleaseNumber = ENDIAN_SWAP32(pCmd->FWReleaseNumber);
    mwlp->hwData.maxNumTXdesc    = ENDIAN_SWAP16(pCmd->NumOfWCB);
    mwlp->hwData.maxNumMCaddr    = ENDIAN_SWAP16(pCmd->NumOfMCastAddr);
    mwlp->hwData.numAntennas     = ENDIAN_SWAP16(pCmd->NumberOfAntenna);
    mwlp->hwData.hwVersion       = pCmd->Version; 
    mwlp->hwData.hostInterface   = pCmd->HostIf; 

    MWL_DBG_EXIT_INFO(DBG_CLASS_HCMD, 
        "region code is %i (0x%x), HW version is %i (0x%x)",
        mwlp->hwData.regionCode, mwlp->hwData.regionCode,
        mwlp->hwData.hwVersion, mwlp->hwData.hwVersion);

    spin_unlock(&mwlp->locks.fwLock);
    return MWL_OK;
}

int
mwl_setFwRadio(struct net_device *netdev, u_int16_t mode, mwl_preamble_e preamble)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_DS_802_11_RADIO_CONTROL *pCmd = 
        (HostCmd_DS_802_11_RADIO_CONTROL *)&mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER_INFO(DBG_CLASS_HCMD,"mode: %s,preamble: %i",
        (mode == MWL_DISABLE) ? "disable" : "enable" , preamble);

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_DS_802_11_RADIO_CONTROL));
    pCmd->CmdHdr.Cmd   =ENDIAN_SWAP16(HostCmd_CMD_802_11_RADIO_CONTROL);
    pCmd->CmdHdr.Length=ENDIAN_SWAP16(sizeof(HostCmd_DS_802_11_RADIO_CONTROL));
    pCmd->Action       =ENDIAN_SWAP16(MWL_SET);
    pCmd->Control      =ENDIAN_SWAP16(preamble);
    pCmd->RadioOn      =ENDIAN_SWAP16(mode);

    if (mode == MWL_DISABLE) {
        pCmd->Control = 0;
    }

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD, (void *) pCmd, 
        sizeof(HostCmd_DS_802_11_RADIO_CONTROL));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_802_11_RADIO_CONTROL);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwAntenna(struct net_device *netdev, mwl_antennatype_e dirSet)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_DS_802_11_RF_ANTENNA *pCmd = 
        (HostCmd_DS_802_11_RF_ANTENNA *)&mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER_INFO(DBG_CLASS_HCMD,
        "will set %s antenna", (dirSet == MWL_ANTENNATYPE_RX)?"RX":"TX");

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_DS_802_11_RF_ANTENNA));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_802_11_RF_ANTENNA);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_DS_802_11_RF_ANTENNA));
    pCmd->Action        = ENDIAN_SWAP16(dirSet);
    if (dirSet == MWL_ANTENNATYPE_RX) {
        pCmd->AntennaMode = ENDIAN_SWAP16(MWL_ANTENNAMODE_RX);
    } else { 
        pCmd->AntennaMode = ENDIAN_SWAP16(MWL_ANTENNAMODE_TX);
    }

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD, (void *) pCmd, 
        sizeof(HostCmd_DS_802_11_RF_ANTENNA));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_802_11_RF_ANTENNA);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwRTSThreshold(struct net_device *netdev, int threshold)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_DS_802_11_RTS_THSD *pCmd = 
        (HostCmd_DS_802_11_RTS_THSD *)&mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER_INFO(DBG_CLASS_HCMD,
        "RTS threshold: %i (0x%x)", threshold, threshold);

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_DS_802_11_RTS_THSD));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_802_11_RTS_THSD);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_DS_802_11_RTS_THSD));
    pCmd->Action  = ENDIAN_SWAP16(MWL_SET);
    if ((threshold >= IEEE80211_RTS_MIN) && (threshold <= IEEE80211_RTS_MAX)) {
        pCmd->Threshold = ENDIAN_SWAP16(threshold);
    } else {
        pCmd->Threshold = ENDIAN_SWAP16(IEEE80211_RTS_MAX);
    }

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD, (void *) pCmd, 
        sizeof(HostCmd_DS_802_11_RTS_THSD));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_802_11_RTS_THSD);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int 
mwl_setFwInfrastructureMode(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_FW_SET_INFRA_MODE *pCmd = 
        (HostCmd_FW_SET_INFRA_MODE *)&mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER(DBG_CLASS_HCMD);

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_FW_SET_INFRA_MODE));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_SET_INFRA_MODE);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_FW_SET_INFRA_MODE));

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD, (void *) pCmd, 
        sizeof(HostCmd_FW_SET_INFRA_MODE));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_SET_INFRA_MODE);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwRate(struct net_device *netdev, mwl_rate_e rate)
{
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;
    HostCmd_FW_SET_RATE *pCmd = (HostCmd_FW_SET_RATE *) &mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */
    int maxNumIdxEntries;
    int currRateIndex;
    int currEntry;
    static const struct {
        u_int8_t   fixedRateIdx;
        u_int8_t   fwClearIdx;
        u_int8_t   fwClearLen;
    } fixedRateFwIdx[] = {
        { MWL_RATE_1_0MBPS,   1 , 12 },
        { MWL_RATE_2_0MBPS,   2 , 11 },
        { MWL_RATE_5_5MBPS,   3 , 10 },
        { MWL_RATE_11_0MBPS,  4 ,  9 },
        { MWL_RATE_6_0MBPS,   6 ,  7 },
        { MWL_RATE_9_0MBPS,   7 ,  6 },
        { MWL_RATE_12_0MBPS,  8 ,  5 },
        { MWL_RATE_18_0MBPS,  9 ,  4 },
        { MWL_RATE_24_0MBPS, 10 ,  3 },
        { MWL_RATE_36_0MBPS, 11 ,  2 },
        { MWL_RATE_48_0MBPS, 12 ,  1 },
        { MWL_RATE_54_0MBPS, 13 ,  0 }
    };

    MWL_DBG_ENTER(DBG_CLASS_HCMD);

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_FW_SET_RATE));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_SET_RATE);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_FW_SET_RATE));
 
    /* note that datarates are expressed in units of 0.5mbps! */
    if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
        memset(&pCmd->ApRates[0], MWL_RATE_AP_EVALUATED, 13);
        maxNumIdxEntries = sizeof(fixedRateFwIdx) / sizeof(fixedRateFwIdx[0]);
        for (currEntry = 0; currEntry < maxNumIdxEntries; currEntry++) {
            if (ic->ic_sup_rates[ic->ic_curmode].rs_rates[ic->ic_fixed_rate] ==
               fixedRateFwIdx[currEntry].fixedRateIdx) {
                memset(&pCmd->ApRates[fixedRateFwIdx[currEntry].fwClearIdx], 
                       0, fixedRateFwIdx[currEntry].fwClearLen);
            }
        }
    } else {
        pCmd->ApRates[0]  = MWL_RATE_1_0MBPS;
        pCmd->ApRates[1]  = MWL_RATE_2_0MBPS;
        pCmd->ApRates[2]  = MWL_RATE_5_5MBPS;
        pCmd->ApRates[3]  = MWL_RATE_11_0MBPS;
        if (ic->ic_curmode == IEEE80211_MODE_11G) {
            pCmd->ApRates[4]  = MWL_RATE_GAP;
            pCmd->ApRates[5]  = MWL_RATE_6_0MBPS;
            pCmd->ApRates[6]  = MWL_RATE_9_0MBPS;
            pCmd->ApRates[7]  = MWL_RATE_12_0MBPS;
            pCmd->ApRates[8]  = MWL_RATE_18_0MBPS;
            pCmd->ApRates[9]  = MWL_RATE_24_0MBPS;
            pCmd->ApRates[10] = MWL_RATE_36_0MBPS;
            pCmd->ApRates[11] = MWL_RATE_48_0MBPS;
            pCmd->ApRates[12] = MWL_RATE_54_0MBPS;
        }
    }

    if (rate != MWL_RATE_AUTOSELECT) {
        for (currRateIndex = 0; currRateIndex < 13; currRateIndex++) {
            if (pCmd->ApRates[currRateIndex] == rate) {
               pCmd->RateIndex = currRateIndex; /* 8bit - no swap */
               pCmd->DataRateType = 1;          /* 8bit - no swap */
            }
        }
        MWL_DBG_INFO(DBG_CLASS_HCMD,"rate %i is at %i",rate,pCmd->RateIndex);
    }

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD,(void *)pCmd,sizeof(HostCmd_FW_SET_RATE));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_SET_RATE);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwSlot(struct net_device *netdev, mwl_slot_e slot)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_FW_SET_SLOT *pCmd = (HostCmd_FW_SET_SLOT *) &mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER_INFO(DBG_CLASS_HCMD,
        "set slot: %s", (slot == MWL_SHORTSLOT) ? "short" : "long");

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_FW_SET_SLOT));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_802_11_SET_SLOT);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_FW_SET_SLOT));
    pCmd->Action        = ENDIAN_SWAP16(MWL_SET);
    pCmd->Slot          = slot;

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD,(void *) pCmd,sizeof(HostCmd_FW_SET_SLOT));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_802_11_SET_SLOT);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

/*******************************************************************************
 * The TX power to be configured is indicated as a percentage value.
 * This percentage value needs to be converted to suit the FW command,
 * because the HostCmd_CMD_802_11_RF_TX_POWER command supports only 
 * three different values. As a consequence, this function will convert
 * the TX powerlevel as follows:
 *   
 *   100% ... 60%   ---> TX_POWER_LEVEL_HIGH   (high TX power level)
 *    59% ... 30%   ---> TX_POWER_LEVEL_MEDIUM (medium TX power level)
 *    29% ...  0%   ---> TX_POWER_LEVEL_LOW    (low TX power level)
 *
 * A value of 0% corresponds to a disabled radio.
 ******************************************************************************/
int
mwl_setFwTxPower(struct net_device *netdev, unsigned int powerLevel)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_DS_802_11_RF_TX_POWER *pCmd = 
        (HostCmd_DS_802_11_RF_TX_POWER *) &mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER_INFO(DBG_CLASS_HCMD, "powerlevel: %i", powerLevel);

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_DS_802_11_RF_TX_POWER));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_802_11_RF_TX_POWER);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_DS_802_11_RF_TX_POWER));
    pCmd->Action        = ENDIAN_SWAP16(MWL_SET);

    if ((powerLevel >= 0) && (powerLevel < 30)) {
        pCmd->SupportTxPowerLevel = ENDIAN_SWAP16(MWL_TX_POWERLEVEL_LOW);
    } else if ((powerLevel >= 30) && (powerLevel < 60)) {
        pCmd->SupportTxPowerLevel = ENDIAN_SWAP16(MWL_TX_POWERLEVEL_MEDIUM);
    } else {
        pCmd->SupportTxPowerLevel = ENDIAN_SWAP16(MWL_TX_POWERLEVEL_HIGH);
    }

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD, (void *) pCmd,
        sizeof(HostCmd_DS_802_11_RF_TX_POWER));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_802_11_RF_TX_POWER);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwPrescan(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_FW_SET_PRESCAN *pCmd = (HostCmd_FW_SET_PRESCAN *) &mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER(DBG_CLASS_HCMD);

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_FW_SET_PRESCAN));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_SET_PRE_SCAN);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_FW_SET_PRESCAN));

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD, (void *) pCmd,
        sizeof(HostCmd_FW_SET_PRESCAN));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_SET_PRE_SCAN);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwPostscan(struct net_device *netdev, u_int8_t *macAddr, u_int8_t ibbsOn)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_FW_SET_POSTSCAN *pCmd =
        (HostCmd_FW_SET_POSTSCAN *) &mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER_INFO(DBG_CLASS_HCMD,
        "MAC addr: %s, ibbsOn: %i", ether_sprintf(macAddr), ibbsOn);

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_FW_SET_POSTSCAN));
    memcpy(&pCmd->BssId, macAddr, ETH_ALEN);
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_SET_POST_SCAN);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_FW_SET_POSTSCAN));
    pCmd->IsIbss        = ENDIAN_SWAP32((ibbsOn) ? 1 : 0); 

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD, (void *) pCmd,
        sizeof(HostCmd_FW_SET_POSTSCAN));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_SET_POST_SCAN);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwMulticast(struct net_device *netdev, struct dev_mc_list *mcAddr)
{
    struct mwl_private *mwlp = netdev->priv;
    int retval = MWL_ERROR; /* initial default */
    unsigned int num = 0;
    HostCmd_DS_MAC_MULTICAST_ADR *pCmd =
        (HostCmd_DS_MAC_MULTICAST_ADR *) &mwlp->pCmdBuf[0];

    MWL_DBG_ENTER(DBG_CLASS_HCMD);

    if (netdev->mc_count == 0) {
        MWL_DBG_EXIT_WARNING(DBG_CLASS_HCMD, "set of 0 multicast addresses");
        return MWL_OK; /* fake OK->0 MC may be ok, but corrupts FW if sent */
    }

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_DS_MAC_MULTICAST_ADR));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_MAC_MULTICAST_ADR);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_DS_MAC_MULTICAST_ADR));
    for (; num < netdev->mc_count ; mcAddr = mcAddr->next) {
        memcpy(&pCmd->MACList[(num*ETH_ALEN)],&mcAddr->dmi_addr[0],ETH_ALEN);
        num++;
        if (num > HostCmd_MAX_MCAST_ADRS) {
            break;
        }
    }
    pCmd->NumOfAdrs = ENDIAN_SWAP16(num);
    pCmd->Action    = ENDIAN_SWAP16(0xffff); /* we set all filter bits */

    MWL_DBG_EXIT_INFO_DATA(DBG_CLASS_HCMD, 
        (void *) pCmd, sizeof(HostCmd_DS_MAC_MULTICAST_ADR),
        "will set %i multicast addresses", ENDIAN_SWAP16(pCmd->NumOfAdrs));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_MAC_MULTICAST_ADR);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwWepKey(struct net_device *netdev, const struct ieee80211_key *key, mwl_operation_e action)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_DS_FW_SET_WEP *pCmd = (HostCmd_DS_FW_SET_WEP *) &mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER(DBG_CLASS_HCMD);

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_DS_FW_SET_WEP));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_SET_WEP);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_DS_FW_SET_WEP));
    pCmd->Key.Len       = key->wk_keylen;
    pCmd->Key.Flags     = key->wk_flags;
    pCmd->Key.Index     = key->wk_keyix; /* range from 0...3 */
    memcpy(pCmd->Key.Value, key->wk_key, key->wk_keylen);

    if (action == MWL_SET) {
        pCmd->Action = ENDIAN_SWAP16(MWL_SET_RC4);
    } else {
        pCmd->Action = ENDIAN_SWAP16(MWL_RESETKEY);
    }
    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD, (void *) pCmd,
        sizeof(HostCmd_DS_FW_SET_WEP));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_SET_WEP);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int 
mwl_setFwGroupKey(struct net_device *netdev, const struct ieee80211_key *key, mwl_operation_e action)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_DS_802_11_GTK *pCmd = (HostCmd_DS_802_11_GTK *) &mwlp->pCmdBuf[0];
    u_int8_t cipher = key->wk_cipher->ic_cipher;
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER(DBG_CLASS_HCMD);

    if (key->wk_keylen == 0) {
        MWL_DBG_EXIT_WARNING(DBG_CLASS_HCMD, "set of 0 len key");
        return MWL_OK;
    }

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_DS_802_11_GTK));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_802_11_GTK);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_DS_802_11_GTK));
    pCmd->Key.Len       = key->wk_keylen;
    pCmd->Key.Flags     = key->wk_flags;
    pCmd->Key.Index     = key->wk_keyix; /* range from 1...3 */
    memcpy(pCmd->Key.Value, key->wk_key, key->wk_keylen);

    if (action == MWL_SET) {
        if (cipher == IEEE80211_CIPHER_AES_CCM) {
            MWL_DBG_INFO(DBG_CLASS_HCMD, "set AES CCM key");
            pCmd->Action = ENDIAN_SWAP16(MWL_SET_AES); 
        } else {
            MWL_DBG_INFO(DBG_CLASS_HCMD, "set RC4 key");
            pCmd->Action = ENDIAN_SWAP16(MWL_SET_RC4); /* WPA/TKIP */
            memcpy(pCmd->Key.TxMicKey, key->wk_txmic, IEEE80211_WEP_MICLEN);
            memcpy(pCmd->Key.RxMicKey, key->wk_rxmic, IEEE80211_WEP_MICLEN);
        }
    } else {
        pCmd->Action = ENDIAN_SWAP16(MWL_RESETKEY);
    }

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD, (void *) pCmd,
        sizeof(HostCmd_DS_802_11_GTK));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_802_11_GTK);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int 
mwl_setFwPairwiseKey(struct net_device *netdev, const struct ieee80211_key *key, const u_int8_t mac[IEEE80211_ADDR_LEN], mwl_operation_e action)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_DS_802_11_PTK *pCmd = (HostCmd_DS_802_11_PTK *) &mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */
    u_int8_t cipher = key->wk_cipher->ic_cipher;
    u_int8_t nullmac[IEEE80211_ADDR_LEN] = { 0x00,0x00,0x00,0x00,0x00,0x00};
    u_int8_t *oldmac;

    MWL_DBG_ENTER(DBG_CLASS_HCMD);

    if (key->wk_keylen == 0) {
        MWL_DBG_EXIT_WARNING(DBG_CLASS_HCMD, "set of 0 len key");
        return MWL_OK;
    }

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_DS_802_11_PTK));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_802_11_PTK);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_DS_802_11_PTK));
    pCmd->Key.Len       = key->wk_keylen;
    pCmd->Key.Flags     = key->wk_flags;
    pCmd->Key.Index     = key->wk_keyix; /* only 0 supported by ieee80211 */
    memcpy(pCmd->MacAddr, mac, IEEE80211_ADDR_LEN);
    memcpy(pCmd->Key.Value, key->wk_key, key->wk_keylen);

    if (action == MWL_SET) {
        if (addStaEncrKey(mac, key)) {
            MWL_DBG_EXIT_ERROR(DBG_CLASS_HCMD, "could not add key");
            spin_unlock(&mwlp->locks.fwLock);
	    return retval;
	}
        if (cipher == IEEE80211_CIPHER_AES_CCM) {
            MWL_DBG_INFO(DBG_CLASS_HCMD, "set AES CCM key");
            pCmd->Action = ENDIAN_SWAP16(MWL_SET_AES);
        } else {
            MWL_DBG_INFO(DBG_CLASS_HCMD, "set RC4 key");
            pCmd->Action = ENDIAN_SWAP16(MWL_SET_RC4);
            memcpy(pCmd->Key.TxMicKey, key->wk_txmic, IEEE80211_WEP_MICLEN);
            memcpy(pCmd->Key.RxMicKey, key->wk_rxmic, IEEE80211_WEP_MICLEN);
        }
    } else {
        oldmac = delStaEncrKey(key);
        if (memcmp(oldmac, nullmac, IEEE80211_ADDR_LEN) == 0) {
            MWL_DBG_EXIT_ERROR(DBG_CLASS_HCMD, "could not del key");
            spin_unlock(&mwlp->locks.fwLock);
	    return retval;
	}
        memcpy(pCmd->MacAddr, oldmac, IEEE80211_ADDR_LEN);
        MWL_DBG_INFO(DBG_CLASS_HCMD, "reset key");
        pCmd->Action = ENDIAN_SWAP16(MWL_RESETKEY);
    }

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD, (void *) pCmd,
        sizeof(HostCmd_DS_802_11_PTK));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_802_11_PTK);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwMacAddress(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_DS_SET_MAC *pCmd = (HostCmd_DS_SET_MAC *) &mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER(DBG_CLASS_HCMD);

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_DS_SET_MAC));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_SET_MAC_ADDR);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_DS_SET_MAC));
    memcpy(&pCmd->MacAddr[0],netdev->dev_addr,ETH_ALEN);

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD,(void *) pCmd,sizeof(HostCmd_DS_SET_MAC));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_SET_MAC_ADDR);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwStationBeacon(struct net_device *netdev, struct ieee80211_node *ni, u_int8_t startIbss)
{
    struct mwl_private *mwlp = netdev->priv;
    struct mwl_node *mwln = (struct mwl_node *) ni;
    struct ieee80211com *ic = &mwlp->curr80211com;
    HostCmd_FW_SET_BCN_CMD *pCmd = (HostCmd_FW_SET_BCN_CMD *) &mwlp->pCmdBuf[0];
    u_int16_t capInfo = HostCmd_CAPINFO_DEFAULT;
    u_int8_t *suppRates = &pCmd->SupportedRates[0];
    u_int8_t *peerRates = &pCmd->PeerRates[0]; 
    int retval = MWL_ERROR; /* initial default */
    int currRate = 0;

    MWL_DBG_ENTER(DBG_CLASS_HCMD);

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_FW_SET_BCN_CMD));
    pCmd->CmdHdr.Cmd         = ENDIAN_SWAP16(HostCmd_CMD_SET_BEACON);
    pCmd->CmdHdr.Length      = ENDIAN_SWAP16(sizeof(HostCmd_FW_SET_BCN_CMD));

    capInfo |= HostCmd_CAPINFO_SHORT_SLOT;
    capInfo |= HostCmd_CAPINFO_DSSS_OFDM;
    if (ni->ni_capinfo & IEEE80211_CAPINFO_PRIVACY) {
        capInfo |= HostCmd_CAPINFO_PRIVACY;
    }
    if (ni->ni_capinfo & IEEE80211_CAPINFO_IBSS) {
        capInfo |= HostCmd_CAPINFO_IBSS;
    }
    pCmd->Caps = ENDIAN_SWAP16(capInfo);

    pCmd->BcnPeriod          = ENDIAN_SWAP16(ni->ni_intval);
    pCmd->IBSS_AtimWindow    = ENDIAN_SWAP16(0);
    pCmd->DtimPeriod         = 3; // ni->ni_dtimperiod;
    pCmd->RfChannel          = ieee80211_chan2ieee(ic, ni->ni_chan);
    pCmd->GProtection        = ENDIAN_SWAP32((ic->ic_protmode) ? 1 : 0);
    pCmd->StartIbss          = ENDIAN_SWAP32((startIbss) ? 1 : 0);
    pCmd->SsIdLength         = ni->ni_esslen; 
    memcpy(&pCmd->SsId, &ni->ni_essid, ni->ni_esslen);
    memcpy(&pCmd->BssId, &ni->ni_bssid, ETH_ALEN);

    /* 
    ** We ought to check whether we can support the node's rates.
    ** We shouldn't simply assume that we can although that's   
    ** certainly true at the moment (11g)                         
    */
    for (currRate = 0; currRate < ni->ni_rates.rs_nrates; currRate++) {
        if (ni->ni_rates.rs_rates[currRate] != 0) {
            *suppRates++ = ni->ni_rates.rs_rates[currRate] & 0x7f; 
            *peerRates++ = ni->ni_rates.rs_rates[currRate] & 0x7f; 
        }
    }

    /*
    ** copy the prepared beacon buffer into the command buffer
    */
    memcpy(&pCmd->BeaconBuffer[0], 
           mwln->beaconData.pSkBuff->data, 
           mwln->beaconData.pSkBuff->len);

    /* 
    ** the 'BeaconFrameLength' indicates the complete length of 
    ** beacon frame including 2 bytes FW length and the 6 bytes
    ** of the ADDR 4 field.                                      
    */
    pCmd->BeaconFrameLength = mwln->beaconData.pSkBuff->len; 

    /* 
    ** the FW length indicates the complete length of the beacon
    ** payload data without any header information like ADDR1, 
    ** DURATION etc.                                             
    */
    *(u_int16_t*)&pCmd->BeaconBuffer[0] = 
        ENDIAN_SWAP16(pCmd->BeaconFrameLength - 
                      NBR_BYTES_COMPLETE_TXFWHEADER);

    /* 
    ** the 'TimOffset' field indicates the complete lenght of the
    ** beacon including header information such as ADDR1 and    
    ** DURATION, not including both 6 bytes ADDR4 + 2 bytes FW length
    */
    pCmd->TimOffset = 
        ENDIAN_SWAP32(pCmd->BeaconFrameLength - 
                      NBR_BYTES_ADD_TXFWINFO);

    /* 
    ** the 'ProbeRspLen' field indicates the complete length of  
    ** the beacon frame including ADDR1 and DURATION and FCS, but 
    ** without any 6 bytes ADDR4 and 2 bytes FW lenght.                  
    */
    pCmd->ProbeRspLen = 
        ENDIAN_SWAP32(mwln->beaconData.pSkBuff->len -
                      NBR_BYTES_ADD_TXFWINFO +
                      NBR_BYTES_FCS);

    /* 
    ** the 'CfOffset' field indicates the lenght of the beacon  
    ** up to the IBSS parameter set, not including both 6 bytes 
    ** ADDR4 and 2 bytes FW length                             
    */
    pCmd->CfOffset = 
       ENDIAN_SWAP32(mwl_getIBSSoff(pCmd->BeaconBuffer,
                                    pCmd->BeaconFrameLength) -
                     NBR_BYTES_ADD_TXFWINFO);
                                  
    /* 
    ** last thing is to update the 'BcnTime' element in the cmd 
    ** buffer with the same information as in the beaconbuffer 
    */
    memcpy(&pCmd->BcnTime[0], 
           &pCmd->BeaconBuffer[NBR_BYTES_COMPLETE_TXFWHEADER], 
           NBR_BYTES_TIMESTAMP);

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD,(void *) pCmd,
        sizeof(HostCmd_FW_SET_BCN_CMD));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_SET_BEACON);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwApBeacon(struct net_device *netdev, struct ieee80211_node *ni)
{
    struct mwl_private *mwlp = netdev->priv;
    struct ieee80211com *ic = &mwlp->curr80211com;
    struct mwl_node *mwln = (struct mwl_node *) ni;
    HostCmd_DS_AP_BEACON *pCmd = (HostCmd_DS_AP_BEACON *) &mwlp->pCmdBuf[0];
    u_int8_t *basicRates = &pCmd->StartCmd.BssBasicRateSet[0];
    u_int8_t *opRates = &pCmd->StartCmd.OpRateSet[0];
    u_int16_t capInfo = HostCmd_CAPINFO_DEFAULT;
    int retval = MWL_ERROR; /* initial default */
    IbssParams_t *ibssParamSet;
    CfParams_t *cfParamSet;
    DsParams_t *phyDsParamSet;
    Country_t *countrySet;
    int wpaOffset = 0;
    int currEntry = 0;
    int currRate = 0;

    MWL_DBG_ENTER(DBG_CLASS_HCMD);

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_DS_AP_BEACON));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_AP_BEACON);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_DS_AP_BEACON));

    memcpy(&pCmd->StartCmd.SsId[0], &ni->ni_essid, ni->ni_esslen);
    pCmd->StartCmd.BssType    = 1; // 0xffee; /* INFRA: 8bit */
    pCmd->StartCmd.BcnPeriod  = ENDIAN_SWAP16(ni->ni_intval);
    pCmd->StartCmd.DtimPeriod = 3; /* 8bit */

    ibssParamSet = &pCmd->StartCmd.SsParamSet.IbssParamSet;
    ibssParamSet->ElementId  = IEEE80211_ELEMID_IBSSPARMS;
    ibssParamSet->Len        = sizeof(ibssParamSet->AtimWindow);
    ibssParamSet->AtimWindow = ENDIAN_SWAP16(0);

    cfParamSet = &pCmd->StartCmd.SsParamSet.CfParamSet;
    cfParamSet->ElementId            = IEEE80211_ELEMID_CFPARMS;
    cfParamSet->Len                  = sizeof(cfParamSet->CfpCnt) +
                                       sizeof(cfParamSet->CfpPeriod) +
                                       sizeof(cfParamSet->CfpMaxDuration) +
                                       sizeof(cfParamSet->CfpDurationRemaining);
    cfParamSet->CfpCnt               = 0; /* 8bit */
    cfParamSet->CfpPeriod            = 2; /* 8bit */
    cfParamSet->CfpMaxDuration       = ENDIAN_SWAP16(0);
    cfParamSet->CfpDurationRemaining = ENDIAN_SWAP16(0);

    phyDsParamSet = &pCmd->StartCmd.PhyParamSet.DsParamSet;
    phyDsParamSet->ElementId   = IEEE80211_ELEMID_DSPARMS;
    phyDsParamSet->Len         = sizeof(phyDsParamSet->CurrentChan); 
    phyDsParamSet->CurrentChan = ieee80211_chan2ieee(ic, ic->ic_ibss_chan);

    pCmd->StartCmd.ProbeDelay = ENDIAN_SWAP16(10);

    capInfo |= HostCmd_CAPINFO_ESS;
    if (ic->ic_flags & IEEE80211_F_PRIVACY) {
        capInfo |= HostCmd_CAPINFO_PRIVACY;
    }
    if (ic->ic_flags & IEEE80211_F_SHPREAMBLE) {
        capInfo |= HostCmd_CAPINFO_SHORT_PREAMBLE;
    }
    if (ic->ic_flags & IEEE80211_F_SHSLOT) { 
        capInfo |= HostCmd_CAPINFO_SHORT_SLOT;
    }
    pCmd->StartCmd.CapInfo = ENDIAN_SWAP16(capInfo);

    for (currRate = 0; currRate < 4; currRate++) {
        *basicRates++ = ni->ni_rates.rs_rates[currRate] & 0x7f; 
    }

    for (currRate = 0; currRate < ni->ni_rates.rs_nrates; currRate++) {
        if (ni->ni_rates.rs_rates[currRate] != 0) {
            *opRates++ = ni->ni_rates.rs_rates[currRate] & 0x7f; 
        }
    }

    if (mwln->beaconData.pSkBuff != NULL) {
        wpaOffset = mwl_getWPAoff(mwln->beaconData.pSkBuff->data,
                                  mwln->beaconData.pSkBuff->len);
        if (wpaOffset) {
            unsigned char *dest = NULL;
            unsigned char *src = (mwln->beaconData.pSkBuff->data + wpaOffset);
            size_t len = *(mwln->beaconData.pSkBuff->data + wpaOffset + 1) + 2;

            if (mwln->beaconData.pSkBuff->data[wpaOffset] == IEEE80211_ELEMID_VENDOR) { 
                dest = (unsigned char *) &pCmd->StartCmd.RsnIE;
            } else {
                dest = (unsigned char *) &pCmd->StartCmd.Rsn48IE;
                *(mwln->beaconData.pSkBuff->data + wpaOffset + 1) = 20;
            }
            memcpy(dest, src, len);
        }
    }

    countrySet = &pCmd->StartCmd.Country;
    countrySet->ElementId = IEEE80211_ELEMID_COUNTRY;
    for (currEntry = 0; currEntry < MAX_CHANNELMAP_ENTRIES; currEntry++) {
        if (mwlp->hwData.regionCode == channelMap[currEntry].regionCode) {
            struct ChannelInfo_t *channelInfo = &countrySet->ChannelInfo[0];
            countrySet->Len = sizeof(countrySet->CountryStr) + 
                              sizeof(struct ChannelInfo_t);
            memcpy(&countrySet->CountryStr[0], 
                   &channelMap[currEntry].countryString[0], 3); 
            channelInfo->FirstChannelNum= channelMap[currEntry].firstChannelNbr;
            channelInfo->NumOfChannels  = channelMap[currEntry].numberChannels;
            channelInfo->MaxTxPwrLevel  = channelMap[currEntry].maxTxPowerLevel;
        }
    }

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD,(void *) pCmd,
        sizeof(HostCmd_DS_AP_BEACON));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_AP_BEACON);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwFinalizeJoin(struct net_device *netdev, struct ieee80211_node *ni, int copyBeacon)
{
    struct mwl_private *mwlp = netdev->priv;
    struct mwl_node *mwln = (struct mwl_node *) ni;
    HostCmd_FW_SET_FINALIZE_JOIN *pCmd = 
        (HostCmd_FW_SET_FINALIZE_JOIN *) &mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER(DBG_CLASS_HCMD);

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_FW_SET_FINALIZE_JOIN));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_SET_FINALIZE_JOIN);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_FW_SET_FINALIZE_JOIN));
    if ((ni->ni_flags & IEEE80211_NODE_PWR_MGT) == IEEE80211_NODE_PWR_MGT) {
        pCmd->ulSleepPeriod = ENDIAN_SWAP32((u_int32_t) 3); /* 3 bcn periods */
    }

    if (copyBeacon) {
        memcpy(&pCmd->BeaconBuffer[0],
               &mwln->beaconData.pSkBuff->data[24],
               mwln->beaconData.pSkBuff->len - 24); 
    }

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD,(void *) pCmd,
        sizeof(HostCmd_FW_SET_FINALIZE_JOIN));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_SET_FINALIZE_JOIN);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwAid(struct net_device *netdev, u_int8_t *bssId, u_int16_t assocId)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_FW_SET_AID *pCmd = (HostCmd_FW_SET_AID *) &mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER_INFO(DBG_CLASS_HCMD,
        "bssid: %s, association ID: %i", ether_sprintf(bssId), assocId);

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_FW_SET_AID));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_SET_AID);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_FW_SET_AID));
    pCmd->AssocID       = ENDIAN_SWAP16(assocId);
    // pCmd->GProtection   = ENDIAN_SWAP32((ic->ic_protmode) ? 1 : 0);
    // memcpy(&pCmd->ApRates[0], 0, 0);
    memcpy(&pCmd->MacAddr[0], bssId, IEEE80211_ADDR_LEN);

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD,(void *) pCmd,sizeof(HostCmd_FW_SET_AID));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_SET_AID);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwChannel(struct net_device *netdev, u_int8_t channel)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_FW_SET_RF_CHANNEL *pCmd = 
        (HostCmd_FW_SET_RF_CHANNEL *) &mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER_INFO(DBG_CLASS_HCMD, "channel: %i", channel);

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_FW_SET_RF_CHANNEL));
    pCmd->CmdHdr.Cmd     = ENDIAN_SWAP16(HostCmd_CMD_SET_RF_CHANNEL);
    pCmd->CmdHdr.Length  = ENDIAN_SWAP16(sizeof(HostCmd_FW_SET_RF_CHANNEL));
    pCmd->CurrentChannel = channel; /* 8bits -> no swap needed! */

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD, (void *) pCmd,
        sizeof(HostCmd_FW_SET_RF_CHANNEL));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_SET_RF_CHANNEL);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_getFwStatistics(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    struct mwl_priv_stats *pStats = &mwlp->privStats;
    HostCmd_DS_802_11_GET_STAT *pCmd =
        (HostCmd_DS_802_11_GET_STAT *) &mwlp->pCmdBuf[0];

    MWL_DBG_ENTER(DBG_CLASS_HCMD);

    spin_lock(&mwlp->locks.fwLock);
    memset(pStats, 0x00, sizeof(struct mwl_priv_stats));
    memset(pCmd, 0x00, sizeof(HostCmd_DS_802_11_GET_STAT));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_802_11_GET_STAT);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_DS_802_11_GET_STAT));

    MWL_DBG_DATA(DBG_CLASS_HCMD, (void *) pCmd, 
        sizeof(HostCmd_DS_802_11_GET_STAT));
    if (mwl_executeCommand(netdev, HostCmd_CMD_802_11_GET_STAT)) {
        MWL_DBG_EXIT_ERROR(DBG_CLASS_HCMD, "failed execution");
        spin_unlock(&mwlp->locks.fwLock);
        return MWL_ERROR;
    }

    pStats->txRetrySuccesses   += ENDIAN_SWAP32(pCmd->TxRetrySuccesses);
    pStats->txMultRetrySucc    += ENDIAN_SWAP32(pCmd->TxMultipleRetrySuccesses);
    pStats->txFailures         += ENDIAN_SWAP32(pCmd->TxFailures);
    pStats->RTSSuccesses       += ENDIAN_SWAP32(pCmd->RTSSuccesses);
    pStats->RTSFailures        += ENDIAN_SWAP32(pCmd->RTSFailures);
    pStats->ackFailures        += ENDIAN_SWAP32(pCmd->AckFailures);
    pStats->rxDuplicateFrames  += ENDIAN_SWAP32(pCmd->RxDuplicateFrames);
    pStats->FCSErrorCount      += ENDIAN_SWAP32(pCmd->FCSErrorCount);
    pStats->txWatchDogTimeouts += ENDIAN_SWAP32(pCmd->TxWatchDogTimeouts);
    pStats->rxOverflows        += ENDIAN_SWAP32(pCmd->RxOverflows);
    pStats->rxFragErrors       += ENDIAN_SWAP32(pCmd->RxFragErrors);
    pStats->rxMemErrors        += ENDIAN_SWAP32(pCmd->RxMemErrors);
    pStats->pointerErrors      += ENDIAN_SWAP32(pCmd->PointerErrors);
    pStats->txUnderflows       += ENDIAN_SWAP32(pCmd->TxUnderflows);
    pStats->txDone             += ENDIAN_SWAP32(pCmd->TxDone);
    pStats->txDoneBufTryPut    += ENDIAN_SWAP32(pCmd->TxDoneBufTryPut);
    pStats->txDoneBufPut       += ENDIAN_SWAP32(pCmd->TxDoneBufPut);
    pStats->wait4TxBuf         += ENDIAN_SWAP32(pCmd->Wait4TxBuf);
    pStats->txAttempts         += ENDIAN_SWAP32(pCmd->TxAttempts);
    pStats->txSuccesses        += ENDIAN_SWAP32(pCmd->TxSuccesses);
    pStats->txFragments        += ENDIAN_SWAP32(pCmd->TxFragments);
    pStats->txMulticasts       += ENDIAN_SWAP32(pCmd->TxMulticasts);
    pStats->rxNonCtlPkts       += ENDIAN_SWAP32(pCmd->RxNonCtlPkts);
    pStats->rxMulticasts       += ENDIAN_SWAP32(pCmd->RxMulticasts);
    pStats->rxUndecryptFrames  += ENDIAN_SWAP32(pCmd->RxUndecryptableFrames);
    pStats->rxICVErrors        += ENDIAN_SWAP32(pCmd->RxICVErrors);
    pStats->rxExcludedFrames   += ENDIAN_SWAP32(pCmd->RxExcludedFrames);

    MWL_DBG_EXIT(DBG_CLASS_HCMD);
    spin_unlock(&mwlp->locks.fwLock);
    return MWL_OK;
}

int
mwl_setFwApBss(struct net_device *netdev, mwl_facilitate_e facility)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_DS_BSS_START *pCmd = (HostCmd_DS_BSS_START *) &mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER_INFO(DBG_CLASS_HCMD,
        "AP bss %s", (facility == MWL_ENABLE) ? "enable": "disable");

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_DS_BSS_START));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_BSS_START);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_DS_BSS_START));
    pCmd->Enable        = ENDIAN_SWAP32(facility);

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD, (void *) pCmd,
        sizeof(HostCmd_DS_BSS_START));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_BSS_START);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwApUpdateTim(struct net_device *netdev, u_int16_t assocId, mwl_bool_e set)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_DS_UPDATE_TIM *pCmd = (HostCmd_DS_UPDATE_TIM *) &mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER_INFO(DBG_CLASS_HCMD,
        "association ID: %i %s", assocId,(set == MWL_TRUE)? "update":"noact");

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_DS_UPDATE_TIM));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_UPDATE_TIM);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_DS_UPDATE_TIM));
    pCmd->Aid           = ENDIAN_SWAP16(assocId);
    pCmd->Set           = ENDIAN_SWAP32(set);

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD, (void *) pCmd,
        sizeof(HostCmd_DS_UPDATE_TIM));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_UPDATE_TIM);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwApSsidBroadcast(struct net_device *netdev, mwl_facilitate_e facility)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_DS_SSID_BROADCAST *pCmd = 
        (HostCmd_DS_SSID_BROADCAST *) &mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER_INFO(DBG_CLASS_HCMD,
        "AP SSID broadcast %s", (facility == MWL_ENABLE) ? "enable":"disable");

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_DS_SSID_BROADCAST));
    pCmd->CmdHdr.Cmd = ENDIAN_SWAP16(HostCmd_CMD_BROADCAST_SSID_ENABLE);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_DS_SSID_BROADCAST));
    pCmd->SsidBroadcastEnable = ENDIAN_SWAP32(facility);

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD, (void *) pCmd,
        sizeof(HostCmd_DS_SSID_BROADCAST));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_BROADCAST_SSID_ENABLE);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwApWds(struct net_device *netdev, mwl_facilitate_e facility)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_DS_WDS *pCmd = (HostCmd_DS_WDS *) &mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER_INFO(DBG_CLASS_HCMD,
        "AP WDS %s", (facility == MWL_ENABLE) ? "enable" : "disable");

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_DS_WDS));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_WDS_ENABLE);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_DS_WDS));
    pCmd->WdsEnable     = ENDIAN_SWAP32(facility);

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD, (void *) pCmd, sizeof(HostCmd_DS_WDS));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_WDS_ENABLE);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwApBurstMode(struct net_device *netdev, mwl_facilitate_e facility)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_DS_BURST_MODE *pCmd = (HostCmd_DS_BURST_MODE *) &mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER_INFO(DBG_CLASS_HCMD,
        "AP burst mode %s", (facility == MWL_ENABLE) ? "enable" : "disable");

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_DS_BURST_MODE));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_SET_BURST_MODE);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_DS_BURST_MODE));
    pCmd->Enable        = ENDIAN_SWAP32(facility);

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD, (void *) pCmd,
        sizeof(HostCmd_DS_BURST_MODE));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_SET_BURST_MODE);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

int
mwl_setFwGProtMode(struct net_device *netdev, mwl_facilitate_e facility)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_FW_SET_G_PROTECT_FLAG *pCmd = 
        (HostCmd_FW_SET_G_PROTECT_FLAG *) &mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER_INFO(DBG_CLASS_HCMD,
        "G prot mode %s", (facility == MWL_ENABLE) ? "enable" : "disable");

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_FW_SET_G_PROTECT_FLAG));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_SET_G_PROTECT_FLAG);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_DS_BURST_MODE));
    pCmd->GProtectFlag  = ENDIAN_SWAP32(facility);

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD, (void *) pCmd,
        sizeof(HostCmd_FW_SET_G_PROTECT_FLAG));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_SET_G_PROTECT_FLAG);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

/*******************************************************************************
 * We have 3 modes which affect the speed:
 *
 * MWL_BOOST_MODE_WIFI - the slowest, but due to conservative timing settings
 *                       also the most reliable.
 *
 * MWL_BOOST_MODE_REGULAR - the normal mode which has some proprietary tweaks
 *                          which provide enhanced performance with most access
 *                          points.
 *
 *                          Apparently this can also cause problems with some
 *                          particularly pedantic APs.
 *
 * MWL_BOOST_MODE_DOUBLE - This is the Marvell proprietary mode which is turned
 *                         on when the driver detects a Marvell access point. 
 *                         A null data frame is sent to the AP with a flag set
 *                         in the LLC/SNAP header to tell the AP that this is a
 *                         Marvell STA. Double Turbo mode can then be turned on,
 *                         providing further speed gains.
 ******************************************************************************/
int
mwl_setFwBoostMode(struct net_device *netdev, mwl_boostmode_e boostmode)
{
    struct mwl_private *mwlp = netdev->priv;
    HostCmd_DS_802_11_BOOST_MODE *pCmd = 
        (HostCmd_DS_802_11_BOOST_MODE *) &mwlp->pCmdBuf[0];
    int retval = MWL_ERROR; /* initial default */

    MWL_DBG_ENTER_INFO(DBG_CLASS_HCMD,
        "boost mode %i (0x%x)", boostmode, boostmode);

    spin_lock(&mwlp->locks.fwLock);
    memset(pCmd, 0x00, sizeof(HostCmd_DS_802_11_BOOST_MODE));
    pCmd->CmdHdr.Cmd    = ENDIAN_SWAP16(HostCmd_CMD_802_11_BOOST_MODE);
    pCmd->CmdHdr.Length = ENDIAN_SWAP16(sizeof(HostCmd_DS_802_11_BOOST_MODE));
    pCmd->Action        = MWL_SET;
    pCmd->ClientMode    = MWL_UNKNOWN_CLIENT_MODE;
    pCmd->Flag          = boostmode;

    MWL_DBG_EXIT_DATA(DBG_CLASS_HCMD, (void *) pCmd,
        sizeof(HostCmd_DS_802_11_BOOST_MODE));
    retval = mwl_executeCommand(netdev, HostCmd_CMD_802_11_BOOST_MODE);
    spin_unlock(&mwlp->locks.fwLock);
    return retval;
}

/*******************************************************************************
 *
 * Local functions
 *
 ******************************************************************************/

static int
mwl_executeCommand(struct net_device *netdev, unsigned short cmd)
{
    MWL_DBG_ENTER_INFO(DBG_CLASS_HCMD, "send cmd 0x%04x to firmware", cmd);

    if (mwl_isAdapterPluggedIn(netdev)) {
        mwl_sendCommand(netdev);
        if (mwl_waitForComplete(netdev, 0x8000 | cmd)) { 
            MWL_DBG_EXIT_ERROR(DBG_CLASS_HCMD, "timeout");
            return MWL_TIMEOUT;
        }
        MWL_DBG_EXIT(DBG_CLASS_HCMD);
        return MWL_OK;
    } 
    MWL_DBG_EXIT_ERROR(DBG_CLASS_HCMD, "no adapter plugged in");
    return MWL_ERROR;
}

static void
mwl_sendCommand(struct net_device *netdev)
{
    struct mwl_private *mwlp = netdev->priv;
    u_int32_t dummy;

    writel(mwlp->pPhysCmdBuf, mwlp->ioBase1+MACREG_REG_GEN_PTR);
    dummy = readl(mwlp->ioBase1 + MACREG_REG_INT_CODE);
    writel(MACREG_H2ARIC_BIT_DOOR_BELL, 
            mwlp->ioBase1+MACREG_REG_H2A_INTERRUPT_EVENTS);
}

static int
mwl_waitForComplete(struct net_device *netdev, u_int16_t cmdCode)
{
    struct mwl_private *mwlp = netdev->priv;
    unsigned int currIteration = MAX_WAIT_FW_COMPLETE_ITERATIONS;
    unsigned short intCode = 0;
 
    do {
        intCode = ENDIAN_SWAP16(mwlp->pCmdBuf[0]);
        mdelay(1); 
    } while ((intCode != cmdCode) && (--currIteration));

    if (currIteration == 0) {
        printk(KERN_ERR "%s: cmd 0x%04x=%s timed out\n", 
            mwl_getDrvName(netdev), cmdCode, mwl_getCmdString(cmdCode));
        return MWL_TIMEOUT;
    } 

    mdelay(3); 
    return MWL_OK;
}

static int
mwl_getIBSSoff(u_int8_t *buff, u_int8_t buffLen)
{
    /* this function return the number of bytes from the start   */
    /* of the passed beacon buffer (including both 6 bytes ADDR4 */
    /* and 2 bytes FW length) up to the IBSS information element */
    int pos = NBR_BYTES_COMPLETE_TXFWHEADER + 
              NBR_BYTES_TIMESTAMP +
              NBR_BYTES_BEACON_INTERVAL +
              NBR_BYTES_CAP_INFO;

    while (pos < buffLen) {
        if (buff[pos] != IEEE80211_ELEMID_IBSSPARMS) {
            pos++; /* one for the element id itself */
            if (buff[pos] != 0) {
                pos = pos + buff[pos]; /* incr # of tokens of this element */
            }
            pos++;
        } else {
            break; 
        }
    }
    return pos; 
}

static int
mwl_getWPAoff(u_int8_t *buff, u_int8_t buffLen)
{
    /* this function return the number of bytes from the start   */
    /* of the passed beacon buffer (including both 6 bytes ADDR4 */
    /* and 2 bytes FW length) up to any WPA information element  */
    /* If no additional WPA inforamtion can be found, a zero is  */
    /* returned. Note that the qualifier of the WPA element is   */
    /* IEEE80211_ELEMID_VENDOR (0xdd) which is misleading...     */

    int pos = NBR_BYTES_IEEE80211HEADER +
              NBR_BYTES_TIMESTAMP +
              NBR_BYTES_BEACON_INTERVAL +
              NBR_BYTES_CAP_INFO;

    while (pos < buffLen) {
        if ((buff[pos] != IEEE80211_ELEMID_VENDOR) &&
            (buff[pos] != IEEE80211_ELEMID_RSN)) { 
            pos++; /* one for the element id itself */
            if (buff[pos] != 0) {
                pos = pos + buff[pos]; /* incr # of tokens of this element */
            }
            pos++;
        } else {
            break; 
        }
    }
    if (pos == buffLen) {
        return 0; /* not found the qualifier... */
    }
    return pos; 
}

static char *
mwl_getCmdString(u_int16_t cmd)
{
    int maxNumCmdEntries = 0;
    int currCmd = 0;
    static const struct {
        u_int16_t   cmdCode;
        char       *cmdString;
    } cmds[] = {
        { HostCmd_CMD_GET_HW_SPEC,           "GetHwSpecifications"    },
        { HostCmd_CMD_802_11_RADIO_CONTROL,  "SetRadio"               },
        { HostCmd_CMD_802_11_RF_ANTENNA,     "SetAntenna"             },
        { HostCmd_CMD_802_11_RTS_THSD,       "SetStationRTSlevel"     },
        { HostCmd_CMD_SET_INFRA_MODE,        "SetInfraMode"           },
        { HostCmd_CMD_SET_RATE,              "SetRate"                },
        { HostCmd_CMD_802_11_SET_SLOT,       "SetStationSlot"         },
        { HostCmd_CMD_802_11_RF_TX_POWER,    "SetTxPower"             },
        { HostCmd_CMD_SET_PRE_SCAN,          "SetPrescan"             },
        { HostCmd_CMD_SET_POST_SCAN,         "SetPostscan"            },
        { HostCmd_CMD_MAC_MULTICAST_ADR,     "SetMulticastAddr"       },
        { HostCmd_CMD_SET_WEP,               "SetWepEncryptionKey"    },
        { HostCmd_CMD_802_11_PTK,            "SetPairwiseTemporalKey" },
        { HostCmd_CMD_802_11_GTK,            "SetGroupTemporalKey"    },
        { HostCmd_CMD_SET_MAC_ADDR,          "SetMACaddress"          },
        { HostCmd_CMD_SET_BEACON,            "SetStationBeacon"       },
        { HostCmd_CMD_AP_BEACON,             "SetApBeacon"            },
        { HostCmd_CMD_SET_FINALIZE_JOIN,     "SetFinalizeJoin"        },
        { HostCmd_CMD_SET_AID,               "SetAid"                 },
        { HostCmd_CMD_SET_RF_CHANNEL,        "SetChannel"             },
        { HostCmd_CMD_802_11_GET_STAT,       "GetFwStatistics"        },
        { HostCmd_CMD_BSS_START,             "SetBSSstart"            },
        { HostCmd_CMD_UPDATE_TIM,            "SetTIM"                 },
        { HostCmd_CMD_BROADCAST_SSID_ENABLE, "SetBroadcastSSID"       },
        { HostCmd_CMD_WDS_ENABLE,            "SetWDS"                 },
        { HostCmd_CMD_SET_BURST_MODE,        "SetBurstMode"           },
        { HostCmd_CMD_SET_G_PROTECT_FLAG,    "SetGprotectionFlag"     },
        { HostCmd_CMD_802_11_BOOST_MODE,     "SetBoostMode"           },
    };

    maxNumCmdEntries = sizeof(cmds) / sizeof(cmds[0]);
    for (currCmd = 0; currCmd < maxNumCmdEntries; currCmd++) {
        if ((cmd & 0x7fff) == cmds[currCmd].cmdCode) {
            return cmds[currCmd].cmdString;
        }
    }
    return "unknown"; /* in case no suitable cmd found in array */
}

static char *
mwl_getCmdResultString(u_int16_t result)
{
    int maxNumResultEntries = 0;
    int currResult = 0;
    static const struct {
        u_int16_t   resultCode;
        char       *resultString;
    } results[] = {
        { HostCmd_RESULT_OK,           "ok"            },
        { HostCmd_RESULT_ERROR,        "general error" },
        { HostCmd_RESULT_NOT_SUPPORT,  "not supported" },
        { HostCmd_RESULT_PENDING,      "pending"       },
        { HostCmd_RESULT_BUSY,         "ignored"       },
        { HostCmd_RESULT_PARTIAL_DATA, "incomplete"    },
    };

    maxNumResultEntries = sizeof(results) / sizeof(results[0]);
    for (currResult = 0; currResult < maxNumResultEntries; currResult++) {
        if (result == results[currResult].resultCode) {
            return results[currResult].resultString;
        }
    }
    return "unknown"; /* in case no suitable result found in array */
}

static char *
mwl_getDrvName(struct net_device *netdev)
{
    if (strchr(netdev->name, '%')) {
        return DRV_NAME;
    }
    return netdev->name;
}

static int 
addStaEncrKey(const u_int8_t mac[IEEE80211_ADDR_LEN], const struct ieee80211_key *key)
{
    if (numStaEncrKeyEntries == MAX_NUM_STA_ENCR_KEY_ENTRIES) {
        return 1;
    }

    memcpy(staEncrKeyMap[numStaEncrKeyEntries].mac, mac, IEEE80211_ADDR_LEN);
    memcpy(staEncrKeyMap[numStaEncrKeyEntries].key, key->wk_key, 
        IEEE80211_KEYBUF_SIZE+IEEE80211_MICBUF_SIZE);
    numStaEncrKeyEntries++;
    return 0;
}

static u_int8_t * 
delStaEncrKey(const struct ieee80211_key *key)
{
    static u_int8_t mac[IEEE80211_ADDR_LEN];
    u_int32_t singleEntryLen = sizeof(struct mwl_stamac_encrkey);
    u_int32_t currEntry;
#define MAP_ENTRY staEncrKeyMap[currEntry]
#define FULL_KEY_SIZE (IEEE80211_KEYBUF_SIZE+IEEE80211_MICBUF_SIZE)

    memset(mac, 0x00, IEEE80211_ADDR_LEN);
    if (numStaEncrKeyEntries == 0) {
        return mac;
    }

    for (currEntry = 0; currEntry < numStaEncrKeyEntries; currEntry++) {
        if (memcmp(MAP_ENTRY.key, key->wk_key, FULL_KEY_SIZE) == 0) {
            memcpy(mac, MAP_ENTRY.mac,IEEE80211_ADDR_LEN);
            if (currEntry != (numStaEncrKeyEntries-1)) { /* not last entry? */
                int copylen = numStaEncrKeyEntries * singleEntryLen -
                                (currEntry+1) * singleEntryLen;
                memcpy(&MAP_ENTRY, (&MAP_ENTRY)+1, copylen);
            }
            numStaEncrKeyEntries--;
            break;
        }
    }
    return mac;
}

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
