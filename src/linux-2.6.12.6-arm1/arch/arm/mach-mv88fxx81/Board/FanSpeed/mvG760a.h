#ifndef __INCmvG760ah
#define __INCmvG760ah

#include "mvTypes.h"
#include "mvTwsi.h"
#include "mvCtrlEnvSpec.h"
#include "mvBoardEnvSpec.h"

#define	CAL_RPM(N) (32768*30)/(N*2)

typedef struct fanspeed
{
	MV_U8  	flag;
	int		program_speed;
	int  	actual_speed;
	int  	status;
} FANSPEED;

//MV_VOID mvFSCG760aSet(MV_U8 rpm);
//MV_VOID mvFSCG760aSet(int rpm);
MV_VOID mvFSCG760aSet(FANSPEED *fanspeed);
//MV_VOID mvFSCG760aGet(MV_U8 *rpm);
MV_VOID mvFSCG760aGet(FANSPEED *fanspeed);


#endif  /* __INCmvG760ah */

