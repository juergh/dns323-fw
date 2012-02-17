/*******************************************************************************
*                Copyright 2004, MARVELL SEMICONDUCTOR, LTD.                   *
* THIS CODE CONTAINS CONFIDENTIAL INFORMATION OF MARVELL.                      *
* NO RIGHTS ARE GRANTED HEREIN UNDER ANY PATENT, MASK WORK RIGHT OR COPYRIGHT  *
* OF MARVELL OR ANY THIRD PARTY. MARVELL RESERVES THE RIGHT AT ITS SOLE        *
* DISCRETION TO REQUEST THAT THIS CODE BE IMMEDIATELY RETURNED TO MARVELL.     *
* THIS CODE IS PROVIDED "AS IS". MARVELL MAKES NO WARRANTIES, EXPRESSED,       *
* IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY, COMPLETENESS OR PERFORMANCE.   *
*                                                                              *
* MARVELL COMPRISES MARVELL TECHNOLOGY GROUP LTD. (MTGL) AND ITS SUBSIDIARIES, *
* MARVELL INTERNATIONAL LTD. (MIL), MARVELL TECHNOLOGY, INC. (MTI), MARVELL    *
* SEMICONDUCTOR, INC. (MSI), MARVELL ASIA PTE LTD. (MAPL), MARVELL JAPAN K.K.  *
* (MJKK), MARVELL ISRAEL LTD. (MSIL).                                          *
*******************************************************************************/

#ifndef __INCmvG751h
#define __INCmvG751h

#include "mvTypes.h"
#include "mvTwsi.h"
#include "mvCtrlEnvSpec.h"
#include "mvBoardEnvSpec.h"

typedef struct temperature
{
	MV_U8  flag;
	MV_U8  temperature;
//     	MV_U8  minutes;

} MV_TEMPERATURE_SENSOR;

//MV_VOID mvTemperSensorG751TemperGet(MV_TEMPERATURE_SENSOR* thermal, int get_type);
MV_VOID mvTemperSensorG751TemperGet(MV_TEMPERATURE_SENSOR* thermal);

MV_VOID mvTemperSensorG751TemperSet(MV_TEMPERATURE_SENSOR* thermal);

#endif  /* __INCmvG751h */

