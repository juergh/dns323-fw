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
 
/* includes */
#include "mvG751.h"
#include "mvG751Reg.h"

//static MV_VOID mvTemperSensorCharWrite(MV_U32 offset, MV_U8 data);
static MV_VOID mvTemperSensorCharWrite(MV_U32 offset, MV_U8 *data);
static MV_VOID mvTemperSensorCharRead(MV_U32 offset, MV_U8 *data, MV_U32 read_byte);

//extern	MV_BOARD_INFO	boardInfoTbl[MV_MAX_BOARD_ID];

/*******************************************************************************
* mvRtcM41T80TimeSet - Update the Real Time Clock.
*
* DESCRIPTION:
*       This function sets a new time and date to the RTC from the given 
*       structure RTC_TIME . All fields within the structure must be assigned 
*		with a value prior to the use of this function.
*
* INPUT:
*       time - A pointer to a structure RTC_TIME (defined in mvM41T80.h).
* OUTPUT:
*       None.
*
* RETURN:
*       None.
*
*******************************************************************************/
MV_VOID mvTemperSensorG751TemperSet(MV_TEMPERATURE_SENSOR* thermal)
{
	int		set_type;
	MV_U32	data[2];
	//MV_U8 data;

	//mvOsPrintf("mvRtcDS1339TimeSet(): -->\n");//Vincent02102006

	set_type = thermal->flag;;
	//data = thermal->temperature;
	data[0] = thermal->temperature;
	data[1] = 0;
	//printk("mvTemperSensorG751TemperSet(): set_type %d,data[0]%d,data[1] %d\n",set_type,data[0],data[1]);
	switch(set_type)
	{
		case THYST_TYPE: //write THYST register
			mvTemperSensorCharWrite(TEMPER_SENSOR_THYST,data);
			break;

		case TOS_TYPE: //write TOS register
			mvTemperSensorCharWrite(TEMPER_SENSOR_TOS,data);
			break;

		default:
			return ;
	}

	return;
}

/*******************************************************************************
* mvTempSensorG751TempGet - Read the temperature from the temperature sensor(G751).
*
* DESCRIPTION:
*       This function reads the current temperature from the G751 into the 
*       structure MV_TEMPERATURE_SENSOR (defined in mvG751.h).
*
* INPUT:
*       thermal - A pointer to a structure MV_TEMPERATURE_SENSOR (defined in mvG751.h).
*		get_type - 0: read temperature register
*				   1: read Thyst register
*				   2: read Tos register	
*
* OUTPUT:
*       The structure MV_TEMPERATURE_SENSOR is updated with the temperature read from the G751.
*
* RETURN:
*       None.
*
*******************************************************************************/
//MV_VOID mvTemperSensorG751TemperGet(MV_TEMPERATURE_SENSOR* thermal, int get_type)
MV_VOID mvTemperSensorG751TemperGet(MV_TEMPERATURE_SENSOR* thermal)
{
	MV_U8 tempValue[2]={0,0};
	MV_U8	minus;
	MV_U32	read_byte=2;
	int		get_type;

#if 0
	mvOsPrintf("<mvG751>: 1 get temperature %x, %x\n",\
				tempValue[0], tempValue[1]);
#endif	
	get_type = thermal->flag;
	
	switch(get_type)
	{
		case TEMPERATURE_TYPE: //read Temperature register
			mvTemperSensorCharRead(TEMPER_SENSOR_TEMPERATURE,tempValue,read_byte);
			break;

		case THYST_TYPE: //read THYST register
			mvTemperSensorCharRead(TEMPER_SENSOR_THYST,tempValue,read_byte);
			break;

		case TOS_TYPE: //read TOS register
			mvTemperSensorCharRead(TEMPER_SENSOR_TOS,tempValue,read_byte);
			break;
		default:
			return ;//-EOPNOTSUPP;
	}

#if 0	
	mvOsPrintf("<mvG751>: 2 get temperature %x, %x\n",\
				tempValue[0], tempValue[1]);
#endif
	
	minus = tempValue[0] & TEMPER_SENSOR_MINUS_MSK;

	if(minus) // temperature < 0
	{
		printk("temperature < 0\n");
	}
	else
	{
		//thermal->temperature = (tempValue[0] & 0x7F)+ ((tempValue[1] & 0x80) >> 7);
		thermal->temperature = (tempValue[0] & 0x7F)+ ((tempValue[1] & 0x80) >> 7);
	}

	return;	
}


/* assumption twsi is initialized !!! */
/*******************************************************************************
* mvTemperSensorCharRead - Read a char from the temperature sensor.
*
* DESCRIPTION:
*       This function reads a char from the temperature sensor offset.
*
* INPUT:
*       offset - offset
*
* OUTPUT:
*       data - char read from offset offset.
*
* RETURN:
*       None.
*
*******************************************************************************/
static MV_VOID	mvTemperSensorCharRead(MV_U32 offset, MV_U8 *data, MV_U32 read_byte)
{
//	MV_TWSI_SLAVE   twsiSlave;
//	MV_U32 boardId;
//
//	//mvOsPrintf("mvRtcCharRead(): -->\n");//Vincent02102006
//	boardId = mvBoardIdGet();
//	
//	twsiSlave.slaveAddr.type = boardInfoTbl[boardId].TemperSensorTwsiAddrType;
//	twsiSlave.slaveAddr.address = boardInfoTbl[boardId].TemperSensorTwsiAddr;
//	twsiSlave.validOffset = MV_TRUE;
//	twsiSlave.offset = offset;
//	twsiSlave.moreThen256 = MV_FALSE;
//	//mvTwsiRead (&twsiSlave, data, 1);
//	mvTwsiRead (&twsiSlave, data, read_byte);
//
//	return;

	MV_TWSI_SLAVE   twsiSlave;
	
	twsiSlave.slaveAddr.type = mvBoardTemperSensorTwsiAddrTypeGet();
	twsiSlave.slaveAddr.address = mvBoardTemperSensorTwsiAddrGet();
	twsiSlave.validOffset = MV_TRUE;
	twsiSlave.offset = offset;
	twsiSlave.moreThen256 = MV_FALSE;
	mvTwsiRead (&twsiSlave, data, 1);

	return;
}

/*******************************************************************************
* mvTemperSensorCharWrite - Write a char from the temperature sensor.
*
* DESCRIPTION:
*       This function writes a char to the temperature sensor offset.
*
* INPUT:
*       offset - offset
*
* OUTPUT:
*       data - char write to addr address.
*
* RETURN:
*       None.
*
*******************************************************************************/
static MV_VOID mvTemperSensorCharWrite(MV_U32 offset, MV_U8 *data)
{
//	MV_TWSI_SLAVE twsiSlave;
//	MV_U32 boardId;
//
//	//mvOsPrintf("mvTemperSensorCharWrite(): offset %d, data %d\n",offset,data);
//	boardId = mvBoardIdGet();
//	
//	twsiSlave.slaveAddr.type = boardInfoTbl[boardId].TemperSensorTwsiAddrType;
//	twsiSlave.slaveAddr.address = boardInfoTbl[boardId].TemperSensorTwsiAddr;
//	//mvOsPrintf("TemperSensorTwsiAddrType %d, TemperSensorTwsiAddr %d\n",boardInfoTbl[boardId].TemperSensorTwsiAddrType,boardInfoTbl[boardId].TemperSensorTwsiAddr);
//	twsiSlave.validOffset = MV_TRUE;
//	twsiSlave.offset = offset;
//	twsiSlave.moreThen256 = MV_FALSE;
//	//mvTwsiWrite (&twsiSlave, &data, 1);
//	mvTwsiWrite (&twsiSlave, data, 2);
//
//	return;
	MV_TWSI_SLAVE twsiSlave;
	
	twsiSlave.slaveAddr.type = mvBoardTemperSensorTwsiAddrTypeGet();
	twsiSlave.slaveAddr.address = mvBoardTemperSensorTwsiAddrGet();
	twsiSlave.validOffset = MV_TRUE;
	twsiSlave.offset = offset;
	twsiSlave.moreThen256 = MV_FALSE;
	mvTwsiWrite (&twsiSlave, data, 2);

	return;
}
