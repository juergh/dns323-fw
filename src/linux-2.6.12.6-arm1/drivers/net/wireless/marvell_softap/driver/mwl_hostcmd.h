/*******************************************************************************
 *
 * Name:        mwl_hostcmd.h
 * Project:     Linux SoftAP for Marvell CB32/CB35 cardbus adapter
 * Version:     $Revision: 1.1.1.1 $
 * Date:        $Date: 2009/08/19 08:02:21 $
 * Author:      Ralph Roesler (rroesler@syskonnect.de)
 * Purpose:     host command header file
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

#ifndef MWL_HOSTCMD_H
#define MWL_HOSTCMD_H

#include <asm/atomic.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include "sys/queue.h"
#include "if_media.h"
#include "ieee80211.h"
#include "ieee80211_crypto.h"
#include "ieee80211_var.h"
#include "ieee80211_node.h"
#include "mwl_debug.h"

/*******************************************************************************
 *
 * Generic driver defines
 *
 ******************************************************************************/

typedef enum {
    MWL_DISABLE = 0,
    MWL_ENABLE = 1,
} mwl_facilitate_e;

typedef enum {
    MWL_LONGSLOT = 0,
    MWL_SHORTSLOT = 1,
} mwl_slot_e;

typedef enum {
    MWL_RATE_AUTOSELECT = 0,
    MWL_RATE_GAP = 0,
    MWL_RATE_AP_EVALUATED = 1,
    MWL_RATE_1_0MBPS = 2,
    MWL_RATE_2_0MBPS = 4,
    MWL_RATE_5_5MBPS = 11,
    MWL_RATE_6_0MBPS = 12,
    MWL_RATE_9_0MBPS = 18,
    MWL_RATE_11_0MBPS = 22,
    MWL_RATE_12_0MBPS = 24,
    MWL_RATE_18_0MBPS = 36,
    MWL_RATE_24_0MBPS = 48,
    MWL_RATE_36_0MBPS = 72,
    MWL_RATE_48_0MBPS = 96,
    MWL_RATE_54_0MBPS = 108,
} mwl_rate_e;

typedef enum {
    MWL_GET = 0,
    MWL_SET = 1,
    MWL_RESET = 2,
} mwl_operation_e;

typedef enum {
    MWL_GET_RC4  = 0, /* WEP & WPA/TKIP algorithm       */
    MWL_SET_RC4  = 1, /* WEP & WPA/TKIP algorithm       */
    MWL_GET_AES  = 2, /* WPA/CCMP & WPA2/CCMP algorithm */
    MWL_SET_AES  = 3, /* WPA/CCMP & WPA2/CCMP algorithm */
    MWL_RESETKEY = 4, /* reset key value to default     */
} mwl_keyaction_e;

typedef enum {
    MWL_BOOST_MODE_REGULAR = 0,
    MWL_BOOST_MODE_WIFI = 1,
    MWL_BOOST_MODE_DOUBLE= 2,
} mwl_boostmode_e;

typedef enum {
    MWL_UNKNOWN_CLIENT_MODE = 0,
    MWL_SINGLE_CLIENT_MODE = 1,
    MWL_MULTIPLE_CLIENT_MODE = 2,
} mwl_boostclientmode_e;

typedef enum {
    MWL_LONG_PREAMBLE = 1,
    MWL_SHORT_PREAMBLE = 3,
    MWL_AUTO_PREAMBLE = 5,
} mwl_preamble_e;

typedef enum {
    MWL_TX_POWERLEVEL_LOW = 5,
    MWL_TX_POWERLEVEL_MEDIUM = 10,
    MWL_TX_POWERLEVEL_HIGH = 15,
} mwl_txpowerlevel_e;

typedef enum {
    MWL_ANTENNATYPE_RX = 1,
    MWL_ANTENNATYPE_TX = 2,
} mwl_antennatype_e;

typedef enum {
    MWL_ANTENNAMODE_RX = 0xffff,
    MWL_ANTENNAMODE_TX = 2,
} mwl_antennamode_e;

extern void mwl_fwCommandComplete(struct net_device *);
extern int mwl_getFwHardwareSpecs(struct net_device *);
extern int mwl_setFwRadio(struct net_device *, u_int16_t, mwl_preamble_e);
extern int mwl_setFwAntenna(struct net_device *, mwl_antennatype_e);
extern int mwl_setFwRTSThreshold(struct net_device *, int);
extern int mwl_setFwInfrastructureMode(struct net_device *);
extern int mwl_setFwRate(struct net_device *, mwl_rate_e);
extern int mwl_setFwSlot(struct net_device *, mwl_slot_e);
extern int mwl_setFwTxPower(struct net_device *, unsigned int);
extern int mwl_setFwPrescan(struct net_device *);
extern int mwl_setFwPostscan(struct net_device *, u_int8_t *, u_int8_t);
extern int mwl_setFwMulticast(struct net_device *, struct dev_mc_list *);
extern int mwl_setFwMacAddress(struct net_device *);
extern int mwl_setFwWepKey(struct net_device *, const struct ieee80211_key *, mwl_operation_e);
extern int mwl_setFwGroupKey(struct net_device *, const struct ieee80211_key *, mwl_operation_e);
extern int mwl_setFwPairwiseKey(struct net_device *, const struct ieee80211_key *,const u_int8_t *, mwl_operation_e );
extern int mwl_setFwStationBeacon(struct net_device *, struct ieee80211_node *, u_int8_t);
extern int mwl_setFwFinalizeJoin(struct net_device *, struct ieee80211_node *, int);
extern int mwl_setFwAid(struct net_device *, u_int8_t *, u_int16_t);
extern int mwl_setFwChannel(struct net_device *, u_int8_t);
extern int mwl_getFwStatistics(struct net_device *);
extern int mwl_setFwApBeacon(struct net_device *, struct ieee80211_node *);
extern int mwl_setFwApBss(struct net_device *, mwl_facilitate_e);
extern int mwl_setFwApUpdateTim(struct net_device *, u_int16_t, mwl_bool_e);
extern int mwl_setFwApSsidBroadcast(struct net_device *, mwl_facilitate_e);
extern int mwl_setFwApWds(struct net_device *, mwl_facilitate_e);
extern int mwl_setFwApBurstMode(struct net_device *, mwl_facilitate_e);
extern int mwl_setFwGProtMode(struct net_device *, mwl_facilitate_e);
extern int mwl_setFwBoostMode(struct net_device *, mwl_boostmode_e);

/*******************************************************************************
 *
 * host command SET defines
 *
 ******************************************************************************/

#define HostCmd_CMD_NONE                       0x0000
#define HostCmd_CMD_CODE_DNLD                  0x0001
#define HostCmd_CMD_GET_HW_SPEC                0x0003
#define HostCmd_CMD_MAC_MULTICAST_ADR          0x0010
#define HostCmd_CMD_SET_WEP                    0x0013
#define HostCmd_CMD_802_11_GET_STAT            0x0014
#define HostCmd_CMD_802_11_RADIO_CONTROL       0x001c
#define HostCmd_CMD_802_11_RF_CHANNEL          0x001d
#define HostCmd_CMD_802_11_RF_TX_POWER         0x001e
#define HostCmd_CMD_802_11_RF_ANTENNA          0x0020
#define HostCmd_CMD_802_11_PTK                 0x0034
#define HostCmd_CMD_802_11_GTK                 0x0035
#define HostCmd_CMD_BROADCAST_SSID_ENABLE      0x0050
#define HostCmd_CMD_SET_BEACON                 0x0100
#define HostCmd_CMD_SET_PRE_SCAN               0x0107
#define HostCmd_CMD_SET_POST_SCAN              0x0108
#define HostCmd_CMD_SET_RF_CHANNEL             0x010a
#define HostCmd_CMD_SET_AID                    0x010d
#define HostCmd_CMD_SET_INFRA_MODE             0x010e
#define HostCmd_CMD_SET_G_PROTECT_FLAG         0x010f
#define HostCmd_CMD_SET_RATE                   0x0110
#define HostCmd_CMD_SET_FINALIZE_JOIN          0x0111
#define HostCmd_CMD_802_11_RTS_THSD            0x0113 
#define HostCmd_CMD_802_11_SET_SLOT            0x0114 
#define HostCmd_CMD_802_11_BOOST_MODE          0x0116
#define HostCmd_CMD_SET_MAC_ADDR               0x0202
#define HostCmd_CMD_BSS_START                  0x1100
#define HostCmd_CMD_AP_BEACON                  0x1101
#define HostCmd_CMD_UPDATE_TIM                 0x1103
#define HostCmd_CMD_WDS_ENABLE                 0x1110
#define HostCmd_CMD_SET_BURST_MODE             0x1113

/*******************************************************************************
 *
 * general result, actions and defines 
 *
 ******************************************************************************/

#define HostCmd_RESULT_OK                      0x0000 /* everything is fine */
#define HostCmd_RESULT_ERROR                   0x0001 /* General error      */
#define HostCmd_RESULT_NOT_SUPPORT             0x0002 /* Command not valid  */
#define HostCmd_RESULT_PENDING                 0x0003 /* will be processed  */
#define HostCmd_RESULT_BUSY                    0x0004 /* Command ignored    */
#define HostCmd_RESULT_PARTIAL_DATA            0x0005 /* Buffer too small   */

#define HostCmd_CAPINFO_DEFAULT                0x0000
#define HostCmd_CAPINFO_ESS                    0x0001
#define HostCmd_CAPINFO_IBSS                   0x0002
#define HostCmd_CAPINFO_CF_POLLABLE            0x0004
#define HostCmd_CAPINFO_CF_REQUEST             0x0008
#define HostCmd_CAPINFO_PRIVACY                0x0010
#define HostCmd_CAPINFO_SHORT_PREAMBLE         0x0020
#define HostCmd_CAPINFO_PBCC                   0x0040
#define HostCmd_CAPINFO_CHANNEL_AGILITY        0x0080
#define HostCmd_CAPINFO_SHORT_SLOT             0x0400
#define HostCmd_CAPINFO_DSSS_OFDM              0x2000

/*******************************************************************************
 *
 * IEEE scan.confirm data structures. (see IEEE 802.11 1999 Spec 10.3.2.2.2)
 * Note that in station FW code, enum type is 1 byte u_int8_t 
 *
 ******************************************************************************/

typedef struct RsnIE_t {
    u_int8_t    ElemId;
    u_int8_t    Len;
    u_int8_t    OuiType[4]; /* 00:50:f2:01 */
    u_int8_t    Ver[2];
    u_int8_t    GrpKeyCipher[4];
    u_int8_t    PwsKeyCnt[2];
    u_int8_t    PwsKeyCipherList[4];
    u_int8_t    AuthKeyCnt[2];
    u_int8_t    AuthKeyList[4];
} __attribute__ ((packed)) RsnIE_t;

typedef struct Rsn48IE_t {
    u_int8_t    ElemId;
    u_int8_t    Len;
    u_int8_t    Ver[2];
    u_int8_t    GrpKeyCipher[4];
    u_int8_t    PwsKeyCnt[2];
    u_int8_t    PwsKeyCipherList[4];
    u_int8_t    AuthKeyCnt[2];
    u_int8_t    AuthKeyList[4];
    u_int8_t    RsnCap[2];
} __attribute__ ((packed)) Rsn48IE_t;

typedef struct CfParams_t {
    u_int8_t    ElementId;
    u_int8_t    Len;
    u_int8_t    CfpCnt;
    u_int8_t    CfpPeriod;
    u_int16_t   CfpMaxDuration;
    u_int16_t   CfpDurationRemaining;
} __attribute__ ((packed)) CfParams_t;

typedef struct IbssParams_t {
    u_int8_t    ElementId;
    u_int8_t    Len;
    u_int16_t   AtimWindow;
} __attribute__ ((packed)) IbssParams_t;

typedef union SsParams_t {
    CfParams_t   CfParamSet;
    IbssParams_t IbssParamSet;
} __attribute__ ((packed)) SsParams_t;

typedef struct FhParams_t {
    u_int8_t    ElementId;
    u_int8_t    Len;
    u_int16_t   DwellTime;
    u_int8_t    HopSet;
    u_int8_t    HopPattern;
    u_int8_t    HopIndex;
} __attribute__ ((packed)) FhParams_t;

typedef struct DsParams_t {
    u_int8_t    ElementId;
    u_int8_t    Len;
    u_int8_t    CurrentChan;
} __attribute__ ((packed)) DsParams_t;

typedef union PhyParams_t {
    FhParams_t  FhParamSet;
    DsParams_t  DsParamSet;
} __attribute__ ((packed)) PhyParams_t;

typedef struct ChannelInfo_t {
    u_int8_t    FirstChannelNum;
    u_int8_t    NumOfChannels;
    u_int8_t    MaxTxPwrLevel;
} __attribute__ ((packed)) ChannelInfo_t;

typedef struct Country_t {
    u_int8_t       ElementId;
    u_int8_t       Len;
    u_int8_t       CountryStr[3];
    ChannelInfo_t  ChannelInfo[40];
} __attribute__ ((packed)) Country_t;

#define IEEEtypes_MAX_DATA_RATES     8
#define IEEEtypes_MAX_DATA_RATES_G  14
#define IEEEtypes_SSID_SIZE	    32
typedef struct StartCmd_t {
    u_int8_t    SsId[IEEEtypes_SSID_SIZE];
    u_int8_t    BssType;
    u_int16_t   BcnPeriod;
    u_int8_t    DtimPeriod;
    SsParams_t  SsParamSet;
    PhyParams_t PhyParamSet;
    u_int16_t   ProbeDelay;
    u_int16_t   CapInfo;
    u_int8_t    BssBasicRateSet[IEEEtypes_MAX_DATA_RATES_G];
    u_int8_t    OpRateSet[IEEEtypes_MAX_DATA_RATES_G];
    RsnIE_t     RsnIE;
    Rsn48IE_t   Rsn48IE;
    Country_t   Country;
    u_int32_t   ApRFType; /* 0->B, 1->G, 2->Mixed, 3->A, 4->11J */
} __attribute__ ((packed)) StartCmd_t;

// #define IEEE80211_KEY_XMIT    0x01   /* key used for xmit */
// #define IEEE80211_KEY_RECV    0x02   /* key used for recv */
// #define IEEE80211_KEY_SWCRYPT 0x04   /* host-based encrypt/decrypt */
// #define IEEE80211_KEY_SWMIC   0x08   /* host-based enmic/demic */
typedef struct KeyInfo_t {
    u_int8_t    Len;
    u_int8_t    Flags;
    u_int16_t   Index; /* -RR: really needed ? */
    u_int8_t    Value[IEEE80211_KEYBUF_SIZE];
    u_int8_t    TxMicKey[IEEE80211_WEP_MICLEN];
    u_int8_t    RxMicKey[IEEE80211_WEP_MICLEN];
    u_int64_t   RxSeqCtr;
    u_int64_t   TxSeqCtr;
} __attribute__ ((packed)) KeyInfo_t;

/*******************************************************************************
 *
 * host command definitions
 *
 ******************************************************************************/

typedef struct tagFWCmdHdr {
    u_int16_t   Cmd;
    u_int16_t   Length;
    u_int16_t   SeqNum;
    u_int16_t   Result; 
} __attribute__ ((packed)) FWCmdHdr;  

typedef struct tagHostCmd_DS_FW_SET_WEP {
    FWCmdHdr    CmdHdr;
    u_int16_t   Action;
    KeyInfo_t   Key;
} __attribute__ ((packed)) HostCmd_DS_FW_SET_WEP;

typedef struct tagHostCmd_DS_802_11_PTK {
    FWCmdHdr    CmdHdr;
    u_int16_t   Action;
    u_int8_t    MacAddr[6];
    KeyInfo_t   Key;
} __attribute__ ((packed)) HostCmd_DS_802_11_PTK;

typedef struct tagHostCmd_DS_802_11_GTK {
    FWCmdHdr    CmdHdr;
    u_int16_t   Action;
    KeyInfo_t   Key;
} __attribute__ ((packed)) HostCmd_DS_802_11_GTK;

#define SIZE_FJ_BEACON_BUFFER 128
typedef struct _HostCmd_FW_SET_FINALIZE_JOIN {
    FWCmdHdr    CmdHdr;
    u_int32_t	ulSleepPeriod; /* Number of beacon periods to sleep */
    u_int8_t    BeaconBuffer[SIZE_FJ_BEACON_BUFFER];
} __attribute__ ((packed)) HostCmd_FW_SET_FINALIZE_JOIN;

typedef struct _HostCmd_DS_GET_HW_SPEC {
    FWCmdHdr    CmdHdr;
    u_int8_t    Version;          /* version of the HW                    */
    u_int8_t    HostIf;           /* host interface                       */
    u_int16_t   NumOfWCB;         /* Max. number of WCB FW can handle     */
    u_int16_t   NumOfMCastAddr;   /* MaxNbr of MC addresses FW can handle */
    u_int8_t    PermanentAddr[6]; /* MAC address programmed in HW         */
    u_int16_t   RegionCode;         
    u_int16_t   NumberOfAntenna;  /* Number of antenna used      */
    u_int32_t   FWReleaseNumber;  /* 4 byte of FW release number */
    u_int32_t   WcbBase0;
    u_int32_t   RxPdWrPtr;
    u_int32_t   RxPdRdPtr;
    u_int32_t   ulFwAwakeCookie;
    u_int32_t   WcbBase1;
    u_int32_t   WcbBase2;
    u_int32_t   WcbBase3;
} __attribute__ ((packed)) HostCmd_DS_GET_HW_SPEC;

#define HostCmd_SIZE_MAC_ADR     6
#define HostCmd_MAX_MCAST_ADRS  32
typedef struct _HostCmd_DS_MAC_MULTICAST_ADR {
    FWCmdHdr    CmdHdr;
    u_int16_t   Action;
    u_int16_t   NumOfAdrs;
    u_int8_t    MACList[HostCmd_SIZE_MAC_ADR*HostCmd_MAX_MCAST_ADRS];
} __attribute__ ((packed)) HostCmd_DS_MAC_MULTICAST_ADR;

typedef struct tagHostCmd_FW_SET_PRESCAN {
    FWCmdHdr    CmdHdr;
    u_int32_t   TsfTime;
} __attribute__ ((packed)) HostCmd_FW_SET_PRESCAN;

typedef struct tagHostCmd_FW_SET_POSTSCAN {
    FWCmdHdr   CmdHdr; /* set the hardware back to its pre Scan state */
    u_int32_t  IsIbss;
    u_int8_t   BssId[6];
} __attribute__ ((packed)) HostCmd_FW_SET_POSTSCAN;

typedef struct tagHostCmd_FW_SET_G_PROTECT_FLAG {
    FWCmdHdr   CmdHdr;
    u_int32_t  GProtectFlag; /* indicate the current state of AP ERP info */
} __attribute__ ((packed)) HostCmd_FW_SET_G_PROTECT_FLAG;

typedef struct tagHostCmd_FW_SET_INFRA_MODE {
    FWCmdHdr   CmdHdr;
} __attribute__ ((packed)) HostCmd_FW_SET_INFRA_MODE;

typedef struct tagHostCmd_FW_RF_CHANNEL {
    FWCmdHdr    CmdHdr;
    u_int16_t   Action;
    u_int8_t    CurrentChannel;
} __attribute__((packed)) HostCmd_FW_SET_RF_CHANNEL;

typedef struct tagHostCmd_FW_SET_SLOT {
    FWCmdHdr    CmdHdr;
    u_int16_t   Action;
    u_int8_t    Slot; /* Slot=0 if regular, Slot=1 if short. */
} __attribute__ ((packed)) HostCmd_FW_SET_SLOT;

typedef struct _HostCmd_DS_802_11_GET_STAT {
    FWCmdHdr    CmdHdr;
    u_int32_t   TxRetrySuccesses;
    u_int32_t   TxMultipleRetrySuccesses;
    u_int32_t   TxFailures;
    u_int32_t   RTSSuccesses;
    u_int32_t   RTSFailures;
    u_int32_t   AckFailures;
    u_int32_t   RxDuplicateFrames;
    u_int32_t   FCSErrorCount;
    u_int32_t   TxWatchDogTimeouts;
    u_int32_t   RxOverflows;
    u_int32_t   RxFragErrors;
    u_int32_t   RxMemErrors;
    u_int32_t   PointerErrors;
    u_int32_t   TxUnderflows;
    u_int32_t   TxDone;
    u_int32_t   TxDoneBufTryPut;
    u_int32_t   TxDoneBufPut;
    u_int32_t   Wait4TxBuf;
    u_int32_t   TxAttempts;
    u_int32_t   TxSuccesses;
    u_int32_t   TxFragments;
    u_int32_t   TxMulticasts;
    u_int32_t   RxNonCtlPkts;
    u_int32_t   RxMulticasts;
    u_int32_t   RxUndecryptableFrames;
    u_int32_t   RxICVErrors;
    u_int32_t   RxExcludedFrames;
} __attribute__ ((packed)) HostCmd_DS_802_11_GET_STAT;

typedef struct _HostCmd_DS_802_11_BOOST_MODE {
    FWCmdHdr    CmdHdr;
    u_int8_t    Action;    
    u_int8_t    Flag;     
    u_int8_t    ClientMode; 
} __attribute__ ((packed)) HostCmd_DS_802_11_BOOST_MODE;

typedef struct _HostCmd_DS_802_11_RADIO_CONTROL {
    FWCmdHdr    CmdHdr;
    u_int16_t   Action;    
    u_int16_t   Control;  
    u_int16_t   RadioOn;
} __attribute__ ((packed)) HostCmd_DS_802_11_RADIO_CONTROL;

#define MAX_CHANNEL_NUMBER 14
typedef struct _HostCmd_DS_802_11_RF_CHANNEL {
    FWCmdHdr    CmdHdr;
    u_int16_t   Action;
    u_int8_t    CurentChannel;
} __attribute__ ((packed)) HostCmd_DS_802_11_RF_CHANNEL;

#define TX_POWER_LEVEL_TOTAL 8
typedef struct _HostCmd_DS_802_11_RF_TX_POWER {
    FWCmdHdr    CmdHdr;
    u_int16_t   Action;
    u_int16_t   SupportTxPowerLevel; /* ranges from min..max => 1..8 */
    u_int16_t   CurrentTxPowerLevel; /* ranges from min..max => 1..8 */
    u_int16_t   Reserved;
    u_int16_t   PowerLevelList[TX_POWER_LEVEL_TOTAL];
} __attribute__ ((packed)) HostCmd_DS_802_11_RF_TX_POWER;

typedef struct _HostCmd_DS_802_11_RF_ANTENNA {
    FWCmdHdr    CmdHdr;
    u_int16_t   Action;
    u_int16_t   AntennaMode; /* Number of antennas or 0xffff (diversity) */
} __attribute__ ((packed)) HostCmd_DS_802_11_RF_ANTENNA;

typedef struct _HostCmd_DS_802_11_RTS_THSD {
    FWCmdHdr    CmdHdr;
    u_int16_t   Action;
    u_int32_t   Threshold;
} __attribute__ ((packed)) HostCmd_DS_802_11_RTS_THSD;

typedef struct tagHostCmd_FW_SET_MAC {
    FWCmdHdr    CmdHdr;
    u_int8_t    MacAddr[6];
} __attribute__ ((packed)) HostCmd_DS_SET_MAC;

#define RATE_INDEX_MAX_ARRAY 14
typedef struct tagHostCmd_FW_SET_BCN_CMD {
    FWCmdHdr    CmdHdr;   /* tagHostCmd_FW_SET_BCN_CMD structure */
    u_int32_t   CfOffset; /* is used to send both start and join */
    u_int32_t   TimOffset;
    u_int16_t   Caps;
    u_int32_t   ProbeRspLen;
    u_int16_t   BcnPeriod;
    u_int16_t   CF_CfpMaxDuration;
    u_int16_t   IBSS_AtimWindow;
    u_int32_t   StartIbss; /* TRUE=start ibss, FALSE=join ibss */
    u_int8_t    BssId[6];
    u_int8_t    BcnTime[8];
    u_int8_t    SsIdLength;
    u_int8_t    SsId[32];
    u_int8_t    SupportedRates[32]; 
    u_int8_t    DtimPeriod;
    u_int8_t    ParamBitMap; /* indicate use of IBSS or CF parameters */
    u_int8_t    CF_CfpCount;
    u_int8_t    CF_CfpPeriod;
    u_int8_t    RfChannel;
    u_int8_t    AccInterval[8];
    u_int8_t    TsfTime[8];
    u_int8_t    BeaconFrameLength;
    u_int8_t    BeaconBuffer[128];
    u_int32_t   GProtection;
    u_int8_t    PeerRates[RATE_INDEX_MAX_ARRAY];
} __attribute__ ((packed)) HostCmd_FW_SET_BCN_CMD;

typedef struct tagHostCmd_FW_SET_AID {
    FWCmdHdr    CmdHdr; /* used for AID sets/clears */
    u_int16_t   AssocID;
    u_int8_t    MacAddr[6];
    u_int32_t   GProtection;
    u_int8_t    ApRates[RATE_INDEX_MAX_ARRAY];
} __attribute__ ((packed)) HostCmd_FW_SET_AID;

typedef struct tagHostCmd_FW_SET_RATE {
    FWCmdHdr    CmdHdr;       /* variable 'DataRateType' has following bits:  */
    u_int8_t    DataRateType; /* @bit0: 0/1 ==>RateAdaptionON/RateAdaptionOFF */
    u_int8_t    RateIndex;    /* Used for fixed rate->contains index if fixed */
    u_int8_t    ApRates[ RATE_INDEX_MAX_ARRAY];
} __attribute__ ((packed)) HostCmd_FW_SET_RATE;

typedef struct tagHostCmd_BSS_START {
    FWCmdHdr    CmdHdr;
    u_int32_t   Enable;   /* FALSE: Disable or TRUE: Enable */
} __attribute__ ((packed)) HostCmd_DS_BSS_START;

typedef struct tagHostCmd_AP_BEACON {
    FWCmdHdr    CmdHdr;
    StartCmd_t  StartCmd;
} __attribute__ ((packed)) HostCmd_DS_AP_BEACON;

typedef struct tagHostCmd_UPDATE_TIM {
    FWCmdHdr    CmdHdr;
    u_int16_t   Aid;
    u_int32_t   Set;
} __attribute__ ((packed)) HostCmd_DS_UPDATE_TIM;

typedef struct tagHostCmd_SSID_BROADCAST {
    FWCmdHdr    CmdHdr;
    u_int32_t   SsidBroadcastEnable;
} __attribute__ ((packed)) HostCmd_DS_SSID_BROADCAST;

typedef struct tagHostCmd_WDS {
    FWCmdHdr    CmdHdr;
    u_int32_t   WdsEnable;
} __attribute__ ((packed)) HostCmd_DS_WDS;

typedef struct tagHostCmd_BURST_MODE { 
    FWCmdHdr    CmdHdr;
    u_int32_t   Enable;
} __attribute__ ((packed)) HostCmd_DS_BURST_MODE;

#endif /* MWL_HOSTCMD_H */

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
