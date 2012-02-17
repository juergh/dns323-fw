/*******************************************************************************
Copyright (C) Marvell International Ltd. and its affiliates

This software file (the "File") is owned and distributed by Marvell 
International Ltd. and/or its affiliates ("Marvell") under the following
alternative licensing terms.  Once you have made an election to distribute the
File under one of the following license alternatives, please (i) delete this
introductory statement regarding license alternatives, (ii) delete the two
license alternatives that you have not elected to use and (iii) preserve the
Marvell copyright notice above.

********************************************************************************
Marvell Commercial License Option

If you received this File from Marvell and you have entered into a commercial
license agreement (a "Commercial License") with Marvell, the File is licensed
to you under the terms of the applicable Commercial License.

********************************************************************************
Marvell GPL License Option

If you received this File from Marvell, you may opt to use, redistribute and/or 
modify this File in accordance with the terms and conditions of the General 
Public License Version 2, June 1991 (the "GPL License"), a copy of which is 
available along with the File in the license.txt file or by writing to the Free 
Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 or 
on the worldwide web at http://www.gnu.org/licenses/gpl.txt. 

THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE IMPLIED 
WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE ARE EXPRESSLY 
DISCLAIMED.  The GPL License provides additional details about this warranty 
disclaimer.
********************************************************************************
Marvell BSD License Option

If you received this File from Marvell, you may opt to use, redistribute and/or 
modify this File under the following licensing terms. 
Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

    *   Redistributions of source code must retain the above copyright notice,
	    this list of conditions and the following disclaimer. 

    *   Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution. 

    *   Neither the name of Marvell nor the names of its contributors may be 
        used to endorse or promote products derived from this software without 
        specific prior written permission. 
    
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR 
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/


#include "mvTypes.h"
#include "mvCommon.h"
#include "mvOs.h"
#include "mvCtrlEnvLib.h"
#include "mvCpu.h"
#include "mvCpuIf.h"
typedef struct _mvSataDecWin
{
    MV_TARGET     target;
    MV_ADDR_WIN   addrWin;    /* An address window*/
    MV_BOOL       enable;     /* Address decode window is enabled/disabled    */
 
} MV_SATA_DEC_WIN;

MV_TARGET sataAddrDecPrioTab[] =
{
    SDRAM_CS0,
    SDRAM_CS1,
    SDRAM_CS2,
    SDRAM_CS3,
    PEX0_MEM,
    PCI0_MEM,
    TBL_TERM
};

#define MV_SATA_MAX_ADDR_DECODE_WIN 4

#define MV_SATA_WIN_CTRL_REG(dev, win)        (SATA_REG_BASE + 0x30 + ((win)<<4))
#define MV_SATA_WIN_BASE_REG(dev, win)        (SATA_REG_BASE + 0x34 + ((win)<<4))

/* BITs in Bridge Interrupt Cause and Mask registers */
#define MV_SATA_ADDR_DECODE_ERROR_BIT        0
#define MV_SATA_ADDR_DECODE_ERROR_MASK       (1<<MV_SATA_ADDR_DECODE_ERROR_BIT)

/* BITs in Windows 0-3 Control and Base Registers */
#define MV_SATA_WIN_ENABLE_BIT               0
#define MV_SATA_WIN_ENABLE_MASK              (1<<MV_SATA_WIN_ENABLE_BIT)

#define MV_SATA_WIN_TARGET_OFFSET            4
#define MV_SATA_WIN_TARGET_MASK              (0xF<<MV_SATA_WIN_TARGET_OFFSET)

#define MV_SATA_WIN_ATTR_OFFSET              8
#define MV_SATA_WIN_ATTR_MASK                (0xFF<<MV_SATA_WIN_ATTR_OFFSET)

#define MV_SATA_WIN_SIZE_OFFSET              16
#define MV_SATA_WIN_SIZE_MASK                (0xFFFF<<MV_SATA_WIN_SIZE_OFFSET)

#define MV_SATA_WIN_BASE_OFFSET              16
#define MV_SATA_WIN_BASE_MASK                (0xFFFF<<MV_SATA_WIN_BASE_OFFSET)

MV_STATUS mvSataWinGet(int dev, MV_U32 winNum, MV_SATA_DEC_WIN *pAddrDecWin);
MV_STATUS mvSataWinSet(int dev, MV_U32 winNum, MV_SATA_DEC_WIN *pAddrDecWin);


/*******************************************************************************
* sataWinOverlapDetect - Detect SATA address windows overlapping
*
* DESCRIPTION:
*       An unpredicted behaviur is expected in case SATA address decode 
*       windows overlapps.
*       This function detects SATA address decode windows overlapping of a 
*       specified window. The function does not check the window itself for 
*       overlapping. The function also skipps disabled address decode windows.
*
* INPUT:
*       winNum      - address decode window number.
*       pAddrDecWin - An address decode window struct.
*
* OUTPUT:
*       None.
*
* RETURN:
*       MV_TRUE if the given address window overlap current address
*       decode map, MV_FALSE otherwise, MV_ERROR if reading invalid data 
*       from registers.
*
*******************************************************************************/
static MV_STATUS sataWinOverlapDetect(int dev, MV_U32 winNum, 
				      MV_ADDR_WIN *pAddrWin)
{
    MV_U32          winNumIndex;
    MV_SATA_DEC_WIN  addrDecWin;

    for(winNumIndex=0; winNumIndex<MV_SATA_MAX_ADDR_DECODE_WIN; winNumIndex++)
    {
        /* Do not check window itself       */
        if (winNumIndex == winNum)
        {
            continue;
        }

        /* Get window parameters    */
        if (MV_OK != mvSataWinGet(dev, winNumIndex, &addrDecWin))
        {
            mvOsPrintf("%s: ERR. TargetWinGet failed\n", __FUNCTION__);
            return MV_ERROR;
        }

        /* Do not check disabled windows    */
        if(addrDecWin.enable == MV_FALSE)
        {
            continue;
        }

        if (MV_TRUE == ctrlWinOverlapTest(pAddrWin, &(addrDecWin.addrWin)))
        {
            return MV_TRUE;
        }        
    }
    return MV_FALSE;
}


/*******************************************************************************
* mvSataWinSet - Set SATA target address window
*
* DESCRIPTION:
*       This function sets a peripheral target (e.g. SDRAM bank0, PCI_MEM0) 
*       address window, also known as address decode window. 
*       After setting this target window, the SATA will be able to access the 
*       target within the address window. 
*
* INPUT:
*       winNum      - SATA target address decode window number.
*       pAddrDecWin - SATA target window data structure.
*
* OUTPUT:
*       None.
*
* RETURN:
*       MV_ERROR if address window overlapps with other address decode windows.
*       MV_BAD_PARAM if base address is invalid parameter or target is 
*       unknown.
*
*******************************************************************************/
MV_STATUS mvSataWinSet(int dev, MV_U32 winNum, MV_SATA_DEC_WIN *pAddrDecWin)
{
    MV_TARGET_ATTRIB    targetAttribs;
    MV_DEC_REGS         decRegs;

    /* Parameter checking   */
    if (winNum >= MV_SATA_MAX_ADDR_DECODE_WIN)
    {
        mvOsPrintf("%s: ERR. Invalid win num %d\n",__FUNCTION__, winNum);
        return MV_BAD_PARAM;
    }
    
    /* Check if the requested window overlapps with current windows         */
    if (MV_TRUE == sataWinOverlapDetect(dev, winNum, &pAddrDecWin->addrWin))
    {
        mvOsPrintf("%s: ERR. Window %d overlap\n", __FUNCTION__, winNum);
        return MV_ERROR;
    }

	/* check if address is aligned to the size */
	if(MV_IS_NOT_ALIGN(pAddrDecWin->addrWin.baseLow, pAddrDecWin->addrWin.size))
	{
		mvOsPrintf("mvSataWinSet:Error setting SATA window %d to "\
				   "target %s.\nAddress 0x%08x is unaligned to size 0x%x.\n",
				   winNum,
				   mvCtrlTargetNameGet(pAddrDecWin->target), 
				   pAddrDecWin->addrWin.baseLow,
				   pAddrDecWin->addrWin.size);
		return MV_ERROR;
	}


    decRegs.baseReg = 0;
    decRegs.sizeReg = 0;

    if (MV_OK != mvCtrlAddrDecToReg(&(pAddrDecWin->addrWin),&decRegs))
    {
        mvOsPrintf("%s: mvCtrlAddrDecToReg Failed\n", __FUNCTION__);
        return MV_ERROR;
    }

    mvCtrlAttribGet(pAddrDecWin->target, &targetAttribs);
                                                                                                                         
    /* set attributes */
    decRegs.sizeReg &= ~MV_SATA_WIN_ATTR_MASK;
    decRegs.sizeReg |= (targetAttribs.attrib << MV_SATA_WIN_ATTR_OFFSET);

    /* set target ID */
    decRegs.sizeReg &= ~MV_SATA_WIN_TARGET_MASK;
    decRegs.sizeReg |= (targetAttribs.targetId << MV_SATA_WIN_TARGET_OFFSET);

    if (pAddrDecWin->enable == MV_TRUE)
    {
        decRegs.sizeReg |= MV_SATA_WIN_ENABLE_MASK;
    }
    else
    {
        decRegs.sizeReg &= ~MV_SATA_WIN_ENABLE_MASK;
    }

    MV_REG_WRITE( MV_SATA_WIN_CTRL_REG(dev, winNum), decRegs.sizeReg);
    MV_REG_WRITE( MV_SATA_WIN_BASE_REG(dev, winNum), decRegs.baseReg);
    
    return MV_OK;
}

/*******************************************************************************
* mvSataWinGet - Get SATA peripheral target address window.
*
* DESCRIPTION:
*       Get SATA peripheral target address window.
*
* INPUT:
*       winNum - SATA target address decode window number.
*
* OUTPUT:
*       pAddrDecWin - SATA target window data structure.
*
* RETURN:
*       MV_ERROR if register parameters are invalid.
*
*******************************************************************************/
MV_STATUS mvSataWinGet(int dev, MV_U32 winNum, MV_SATA_DEC_WIN *pAddrDecWin)
{
    MV_DEC_REGS         decRegs;
    MV_TARGET_ATTRIB    targetAttrib;
                                                                                                                         
    /* Parameter checking   */
    if (winNum >= MV_SATA_MAX_ADDR_DECODE_WIN)
    {
        mvOsPrintf("%s (dev=%d): ERR. Invalid winNum %d\n", 
                    __FUNCTION__, dev, winNum);
        return MV_NOT_SUPPORTED;
    }

    decRegs.baseReg = MV_REG_READ( MV_SATA_WIN_BASE_REG(dev, winNum) );
    decRegs.sizeReg = MV_REG_READ( MV_SATA_WIN_CTRL_REG(dev, winNum) );
 
    if (MV_OK != mvCtrlRegToAddrDec(&decRegs, &pAddrDecWin->addrWin) )
    {
        mvOsPrintf("%s: mvCtrlRegToAddrDec Failed\n", __FUNCTION__);
        return MV_ERROR; 
    }
       
    /* attrib and targetId */
    targetAttrib.attrib = (decRegs.baseReg & MV_SATA_WIN_ATTR_MASK) >> 
		MV_SATA_WIN_ATTR_OFFSET;
    targetAttrib.targetId = (decRegs.baseReg & MV_SATA_WIN_TARGET_MASK) >> 
		MV_SATA_WIN_TARGET_OFFSET;
 
    pAddrDecWin->target = mvCtrlTargetGet(&targetAttrib);

    /* Check if window is enabled   */
    if(decRegs.sizeReg & MV_SATA_WIN_ENABLE_MASK) 
    {
        pAddrDecWin->enable = MV_TRUE;
    }
    else
    {
        pAddrDecWin->enable = MV_FALSE;
    }
    return MV_OK;
}

/*******************************************************************************
* mvSataWinInit - Initialize the integrated SATA target address window.
*
* DESCRIPTION:
*       Initialize the SATA peripheral target address window.
*
* INPUT:
*
*
* OUTPUT:
*     
*
* RETURN:
*       MV_ERROR if register parameters are invalid.
*
*******************************************************************************/
MV_STATUS mvSataWinInit(MV_VOID)
{
    int             winNum;
    MV_SATA_DEC_WIN  sataWin;
    MV_CPU_DEC_WIN  cpuAddrDecWin;
    MV_U32          status, winPrioIndex = 0;
    
    winNum = 0;
    while( (sataAddrDecPrioTab[winPrioIndex] != TBL_TERM) &&
           (winNum < MV_SATA_MAX_ADDR_DECODE_WIN) )
    {
        /* first get attributes from CPU If */
        status = mvCpuIfTargetWinGet(sataAddrDecPrioTab[winPrioIndex],
                                     &cpuAddrDecWin);

        if(MV_NO_SUCH == status)
        {
            winPrioIndex++;
            continue;
        }
		if (MV_OK != status)
		{
            mvOsPrintf("%s: ERR. mvCpuIfTargetWinGet failed\n", __FUNCTION__);
			return MV_ERROR;
		}

        if (cpuAddrDecWin.enable == MV_TRUE)
        {
            sataWin.addrWin.baseHigh = cpuAddrDecWin.addrWin.baseHigh;
            sataWin.addrWin.baseLow  = cpuAddrDecWin.addrWin.baseLow;
            sataWin.addrWin.size     = cpuAddrDecWin.addrWin.size;
            sataWin.enable           = MV_TRUE;
            sataWin.target           = sataAddrDecPrioTab[winPrioIndex];
            
            if(MV_OK != mvSataWinSet(0/*dev*/, winNum, &sataWin))
            {
                return MV_ERROR;
            }
            winNum++;
        }
        winPrioIndex++;
    }
 
	return MV_OK;
}

