/* includes */
#include "mvG760a.h"
#include "mvG760aReg.h"

static MV_VOID mvFanSpeedCtlWrite(MV_U32 offset, MV_U8 data);
static MV_VOID mvFanSpeedCtlRead(MV_U32 offset, MV_U8 *data);

//extern	MV_BOARD_INFO	boardInfoTbl[MV_MAX_BOARD_ID];

//#define	RPM_0		255
//#define	RPM_1935	254
//#define	RPM_2000	246
//#define	RPM_3000	164
//#define	RPM_3500	140
//#define	RPM_4000	123
//#define	RPM_4500	109
//#define	RPM_5000	98
//#define	RPM_5200	95
//#define	RPM_5300	93
//#define	RPM_5400	91
//#define	RPM_5500	89
//#define	RPM_5700	86
//#define	RPM_5800	85
//
//MV_U8 RPM[]=
//{
//	RPM_0,
//	RPM_5000,
//	RPM_5200,
//	RPM_5300,
//	RPM_5400,
//	RPM_5500,
//	RPM_5500,
//	RPM_5700,
//	RPM_5800
//};

/*******************************************************************************
* mvFSCG760aSet - FAN Speed Control setting
*
* DESCRIPTION:
*       This function sets a new rpm value to Fan Speed Controller
*
* INPUT:
*       
* OUTPUT:
*       None.
*
* RETURN:
*       None.
*
*******************************************************************************/
//MV_VOID mvFSCG760aSet(MV_U8 rpm)
MV_VOID mvFSCG760aSet(FANSPEED *fanspeed)
{
	//MV_U8 tempValue;

	//mvOsPrintf("mvRtcDS1339TimeSet(): -->\n");//Vincent02102006
  
	//printk("CAL_RPM(rpm) %d\n",CAL_RPM(fanspeed->program_speed));
	if(fanspeed->program_speed == 0)
		mvFanSpeedCtlWrite(PRO_FAN_SPEED, 255);
	else if(fanspeed->program_speed <= 30000 && fanspeed->program_speed >= 1935)
		mvFanSpeedCtlWrite(PRO_FAN_SPEED, CAL_RPM(fanspeed->program_speed));
	else
	{
		printk("<Wrong rpm speed>\n");
		mvFanSpeedCtlWrite(PRO_FAN_SPEED, 255);
	}

	return;
}

/*******************************************************************************
* mvFSCG760aGet - Read the fan speed from the G760a.
*
* DESCRIPTION:
*       This function reads the current fan speed register status from the G760a
*
* INPUT:
*
* OUTPUT:
*       The structure FANSPEED is updated with the fan speed read from the G760a.
*
* RETURN:
*       None.
*
*******************************************************************************/
//MV_VOID mvFSCG760aGet(MV_U8 *rpm)
MV_VOID mvFSCG760aGet(FANSPEED *fanspeed)
{
	MV_U8 	tempValue;

	mvFanSpeedCtlRead(PRO_FAN_SPEED,&tempValue);
	if(tempValue == 255)
		fanspeed->program_speed = 0;
	else
		fanspeed->program_speed = CAL_RPM((int)tempValue);
	//mvOsPrintf("get program speed %d,%d\n",tempValue,fanspeed->program_speed);

	mvFanSpeedCtlRead(ACTUAL_FAN_SPEED,&tempValue);
	if(tempValue == 255)
		fanspeed->actual_speed = 0;
	else
		fanspeed->actual_speed = CAL_RPM((int)tempValue);
	//mvOsPrintf("get actual speed %d,%d\n",tempValue,fanspeed->actual_speed);

	mvFanSpeedCtlRead(FAN_STATUS,&tempValue);
	fanspeed->status = tempValue;
	//mvOsPrintf("get fan status %d,%d\n",tempValue,fanspeed->status);

	return;	
}

/* assumption twsi is initialized !!! */
/*******************************************************************************
* mvFanSpeedCtlRead - Read a char from the FAN Speed Controller.
*
* DESCRIPTION:
*       This function reads a char from the FAN Speed Controller offset.
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
static MV_VOID	mvFanSpeedCtlRead(MV_U32 offset, MV_U8 *data)
{
//	MV_TWSI_SLAVE   twsiSlave;
//	MV_U32 boardId;
//
//	//mvOsPrintf("mvRtcCharRead(): -->\n");//Vincent02102006
//	boardId = mvBoardIdGet();
//
//	twsiSlave.slaveAddr.type = boardInfoTbl[boardId].FanSpeedCTLTwsiAddrType;
//	twsiSlave.slaveAddr.address = boardInfoTbl[boardId].FanSpeedCTLTwsiAddr;
//	twsiSlave.validOffset = MV_TRUE;
//	twsiSlave.offset = offset;
//	twsiSlave.moreThen256 = MV_FALSE;
//	mvTwsiRead (&twsiSlave, data, 1);
//
//	return;
	MV_TWSI_SLAVE   twsiSlave;
	
	twsiSlave.slaveAddr.type = mvBoardFanSpeedCTLTwsiAddrTypeGet();
	twsiSlave.slaveAddr.address = mvBoardFanSpeedCTLTwsiAddrGet();
	twsiSlave.validOffset = MV_TRUE;
	twsiSlave.offset = offset;
	twsiSlave.moreThen256 = MV_FALSE;
	mvTwsiRead (&twsiSlave, data, 1);

	return;
}

/*******************************************************************************
* mvFanSpeedCtlWrite - Write a char to the FAN Speed Controller.
*
* DESCRIPTION:
*       This function writes a char to the FAN Speed Controller offset.
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
static MV_VOID mvFanSpeedCtlWrite(MV_U32 offset, MV_U8 data)
{
//	MV_TWSI_SLAVE twsiSlave;
//	MV_U32 boardId;
//
//	//mvOsPrintf("mvRtcCharWrite(): -->\n"); //Vincent03212006
//	boardId = mvBoardIdGet();
//	
//	twsiSlave.slaveAddr.type = boardInfoTbl[boardId].FanSpeedCTLTwsiAddrType;
//	twsiSlave.slaveAddr.address = boardInfoTbl[boardId].FanSpeedCTLTwsiAddr;
//	twsiSlave.validOffset = MV_TRUE;
//	twsiSlave.offset = offset;
//	twsiSlave.moreThen256 = MV_FALSE;
//	mvTwsiWrite (&twsiSlave, &data, 1);
//
//	return;
	MV_TWSI_SLAVE twsiSlave;
	
	twsiSlave.slaveAddr.type = mvBoardFanSpeedCTLTwsiAddrTypeGet();
	twsiSlave.slaveAddr.address = mvBoardFanSpeedCTLTwsiAddrGet();
	twsiSlave.validOffset = MV_TRUE;
	twsiSlave.offset = offset;
	twsiSlave.moreThen256 = MV_FALSE;
	mvTwsiWrite (&twsiSlave, &data, 1);

	return;
}
