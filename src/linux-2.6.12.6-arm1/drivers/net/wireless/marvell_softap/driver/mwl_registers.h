/*******************************************************************************
 *
 * Name:        mwl_macreg.h
 * Project:     Linux SoftAP for Marvell CB32/CB35 cardbus adapter
 * Version:     $Revision: 1.1.1.1 $
 * Date:        $Date: 2009/08/19 08:02:21 $
 * Author:      Ralph Roesler (rroesler@syskonnect.de)
 * Purpose:     mac register header file
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

#ifndef MWL_MACREG_H
#define MWL_MACREG_H

#define EAGLE_NDIS_MAJOR_VERSION 0x5

#ifdef MRVL_WINXP_NDIS51
#define EAGLE_NDIS_MINOR_VERSION 0x1
#else
#define EAGLE_NDIS_MINOR_VERSION 0x0
#endif

#define EAGLE_DRIVER_VERSION ((EAGLE_NDIS_MAJOR_VERSION*0x100) + EAGLE_NDIS_MINOR_VERSION)

//
//          Product name
//
#define	VENDORDESCRIPTOR "Marvell W8300 802.11 NIC"

//
//          Define vendor ID and device ID
//
#define MRVL_PCI_VENDOR_ID                  0x11AB // VID
//#define MRVL_8100_PCI_DEVICE_ID             0x1FA4 // DID
#define MRVL_8100_PCI_DEVICE_ID             0x1FAA // DID
//#define MRVL_8100_PCI_DEVICE_ID             0x8310 // DID

//#define MRVL_8100_CARDBUS_DEVICE_ID         0x8101

#define MRVL_8100_PCI_REV_0                 0x00
#define MRVL_8100_PCI_REV_1                 0x01
#define MRVL_8100_PCI_REV_2                 0x02
#define MRVL_8100_PCI_REV_3                 0x03
#define MRVL_8100_PCI_REV_4                 0x04
#define MRVL_8100_PCI_REV_5                 0x05
#define MRVL_8100_PCI_REV_6                 0x06
#define MRVL_8100_PCI_REV_7                 0x07
#define MRVL_8100_PCI_REV_8                 0x08
#define MRVL_8100_PCI_REV_9                 0x09
#define MRVL_8100_PCI_REV_a                 0x0a
#define MRVL_8100_PCI_REV_b                 0x0b
#define MRVL_8100_PCI_REV_c                 0x0c
#define MRVL_8100_PCI_REV_d                 0x0d
#define MRVL_8100_PCI_REV_e                 0x0e
#define MRVL_8100_PCI_REV_f                 0x0f

//          The following version information is used bysed by OID_GEN_VENDOR_ID
#define MRVL_8100_PCI_VER_ID               0x00
#define MRVL_8100_CARDBUS_VER_ID           0x01

//
//          Define station register offset
//

//          Map to 0x80000000 (Bus control) on BAR0
#define MACREG_REG_H2A_INTERRUPT_EVENTS     	0x00000C18 // (From host to ARM)
#define MACREG_REG_H2A_INTERRUPT_CAUSE      	0x00000C1C // (From host to ARM)
#define MACREG_REG_H2A_INTERRUPT_MASK       	0x00000C20 // (From host to ARM)
#define MACREG_REG_H2A_INTERRUPT_CLEAR_SEL      0x00000C24 // (From host to ARM)
#define MACREG_REG_H2A_INTERRUPT_STATUS_MASK	0x00000C28 // (From host to ARM)

#define MACREG_REG_A2H_INTERRUPT_EVENTS     	0x00000C2C // (From ARM to host)
#define MACREG_REG_A2H_INTERRUPT_CAUSE      	0x00000C30 // (From ARM to host)
#define MACREG_REG_A2H_INTERRUPT_MASK       	0x00000C34 // (From ARM to host)
#define MACREG_REG_A2H_INTERRUPT_CLEAR_SEL      0x00000C38 // (From ARM to host)
#define MACREG_REG_A2H_INTERRUPT_STATUS_MASK    0x00000C3C // (From ARM to host)


//  Map to 0x80000000 on BAR1
#define MACREG_REG_GEN_PTR                  0x00000C10
#define MACREG_REG_INT_CODE                 0x00000C14

#define MACREG_REG_FW_PRESENT				0x0000BFFC


//	Bit definitio for MACREG_REG_A2H_INTERRUPT_CAUSE (A2HRIC)
#define MACREG_A2HRIC_BIT_TX_DONE           0x00000001 // bit 0
#define MACREG_A2HRIC_BIT_RX_RDY            0x00000002 // bit 1
#define MACREG_A2HRIC_BIT_OPC_DONE          0x00000004 // bit 2
#define MACREG_A2HRIC_BIT_MAC_EVENT         0x00000008 // bit 3
#define MACREG_A2HRIC_BIT_RX_PROBLEM        0x00000010 // bit 4

#define MACREG_A2HRIC_BIT_RADIO_OFF        	0x00000020 // bit 5
#define MACREG_A2HRIC_BIT_RADIO_ON        	0x00000040 // bit 6

#define MACREG_A2HRIC_BIT_MASK              0x0000007F

//	Bit definitio for MACREG_REG_H2A_INTERRUPT_CAUSE (H2ARIC)
#define MACREG_H2ARIC_BIT_PPA_READY         0x00000001 // bit 0
#define MACREG_H2ARIC_BIT_DOOR_BELL         0x00000002 // bit 1
#define MACREG_H2ARIC_BIT_PS         		0x00000004 // bit 2
#define MACREG_H2ARIC_BIT_PSPOLL       		0x00000008 // bit 3
//#define MACREG_H2ARIC_BIT_MASK              0x00000007
#define ISR_RESET           				(1<<15)
#define ISR_RESET_AP33                                  (1<<26)

// Power Save events
#define MACREG_PS_OFF						0x00000000
#define MACREG_PS_ON						0x00000001

//	INT code register event definition
#define MACREG_INT_CODE_TX_PPA_FREE         0x00000000 
#define MACREG_INT_CODE_TX_DMA_DONE         0x00000001
#define MACREG_INT_CODE_LINK_LOSE_W_SCAN    0x00000002
#define MACREG_INT_CODE_LINK_LOSE_NO_SCAN   0x00000003
#define MACREG_INT_CODE_LINK_SENSED         0x00000004
#define MACREG_INT_CODE_CMD_FINISHED        0x00000005
#define MACREG_INT_CODE_MIB_CHANGED         0x00000006 
#define MACREG_INT_CODE_INIT_DONE           0x00000007 
#define MACREG_INT_CODE_DEAUTHENTICATED     0x00000008 
#define MACREG_INT_CODE_DISASSOCIATED       0x00000009 

#endif /* MWL_MACREG_H */

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
