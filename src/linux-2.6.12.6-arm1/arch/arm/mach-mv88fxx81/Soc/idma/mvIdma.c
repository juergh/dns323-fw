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

/*******************************************************************************
* mvIdma.h - Implementation file for IDMA HW library
*
* DESCRIPTION:
*       This file contains Marvell Controller IDMA HW library API 
*       implementation.
*       NOTE: 
*       1) This HW library API assumes IDMA source, destination and 
*          descriptors are cache coherent. 
*       2) In order to gain high performance, the API does not parform 
*          API parameter checking.
*
* DEPENDENCIES:
*       None.
*
*******************************************************************************/
#if defined(MV_88F5181)

/* includes */
#include "mvIdma.h"
#include "mvCpuIf.h"

/* defines  */
#ifdef MV_DEBUG
	#define DB(x)	x
#else
	#define DB(x)
#endif	

/* typedefs */

/* Locals */

MV_TARGET dmaAddrDecPrioTap[] = 
{
  	DEVICE_CS0,
	PEX0_MEM,
	PCI0_MEM,
	PCI0_IO,
	SDRAM_CS0,
	SDRAM_CS1,
	SDRAM_CS2,
	SDRAM_CS3,

#if defined(MV_88F5182) || defined(MV_88F5181L)
    CRYPT_ENG,
#endif /* defined(MV_88F5182) || defined(MV_88F5181L) */
	DEV_BOOCS,
   	DEVICE_CS1,
   	DEVICE_CS2,
   	TBL_TERM
};

static MV_STATUS dmaWinOverlapDetect(MV_U32 winNum, MV_ADDR_WIN *pAddrWin);

/*******************************************************************************
* mvDmaInit - Initialize IDMA engine
*
* DESCRIPTION:
*		This function initialize IDMA unit. It set the default address decode
*		windows of the unit.
*
* INPUT:
*       None.
*
* OUTPUT:
*       None.
*
* RETURN:
*       MV_ERROR if setting fail.
*******************************************************************************/
MV_STATUS mvDmaInit (MV_VOID)
{
    	MV_U32      winNum, status;
	MV_U32      dmaChanNum;
	MV_DMA_DEC_WIN idmaWin;
	MV_CPU_DEC_WIN cpuAddrDecWin;
	MV_U32		winPrioIndex=0;

	/* Initiate IDMA address decode */

	/* First disable all address decode windows */
	MV_REG_WRITE(IDMA_BASE_ADDR_ENABLE_REG, IBAER_ENABLE_MASK);
	
	
	/* Go through all windows in user table until table terminator			*/
	for (winNum = 0; ((dmaAddrDecPrioTap[winPrioIndex] != TBL_TERM) && 
					  (winNum < IDMA_MAX_ADDR_DEC_WIN)); )
    {
        /* first get attributes from CPU If */
		status = mvCpuIfTargetWinGet(dmaAddrDecPrioTap[winPrioIndex], 
									 &cpuAddrDecWin);
        if(MV_NO_SUCH == status)
        {
            winPrioIndex++;
            continue;
        }
		if (MV_OK != status)
		{
			mvOsPrintf("mvDmaInit: ERR. mvCpuIfTargetWinGet failed\n");
			return MV_ERROR;
		}

        if (cpuAddrDecWin.enable == MV_TRUE)
		{
			idmaWin.addrWin.baseHigh = cpuAddrDecWin.addrWin.baseHigh;
			idmaWin.addrWin.baseLow = cpuAddrDecWin.addrWin.baseLow;
			idmaWin.addrWin.size = cpuAddrDecWin.addrWin.size;
			idmaWin.enable = MV_TRUE;
		    idmaWin.target = dmaAddrDecPrioTap[winPrioIndex];

		    if(MV_OK != mvDmaWinSet(winNum, &idmaWin))
		    {
			    return MV_ERROR;
		    }
		    winNum++;
		}
		winPrioIndex++;			
    }

	/* Abort any DMA activity */
	for(dmaChanNum = 0; dmaChanNum < MV_IDMA_MAX_CHAN; dmaChanNum++)
	{
        mvDmaCommandSet(dmaChanNum, MV_STOP);
	
#if defined(MV_CPU_LE)
		/* The following must be set */
		mvDmaCtrlHighSet(dmaChanNum, 
						 (ICCHR_ENDIAN_LITTLE | ICCHR_DESC_BYTE_SWAP_EN));
#endif
	}
    MV_REG_WRITE( IDMA_CAUSE_REG, 0);   

    return MV_OK;
}

/*******************************************************************************
* mvDmaCtrlLowSet - Set IDMA channel control low register
*
* DESCRIPTION:
*       Each IDMA Channel has its own unique control registers (high and low)
*       where certain IDMA modes are programmed.
*       This function writes 32bit word to IDMA control low register.
*
*						!!!!!! WARNING !!!!!!
*		If system uses the IDMA DRAM HW cache coherency the source and 
* 		destination maximum DTL size must be cache line size.
*
* INPUT:
*       chan     - DMA channel number. See MV_DMA_CHANNEL enumerator.
*       ctrlWord - Channel control word for low register.
*
* OUTPUT:
*       None.
*
* RETURN:
*       MV_OK
*
* NOTE: This function can modified the Override attribute that mvDmaOverrideSet
*       configured. 
*******************************************************************************/
MV_STATUS mvDmaCtrlLowSet(MV_U32 chan, MV_U32 ctrlWord)
{
    /* Parameter checking   */
    if (chan >= MV_IDMA_MAX_CHAN)
    {
		mvOsPrintf("mvDmaCtrlLowSet: ERR. Invalid chan num %d\n", chan);
        return MV_ERROR;
    }
    
	MV_REG_WRITE(IDMA_CTRL_LOW_REG(chan), ctrlWord);
	return MV_OK;
}


/*******************************************************************************
* mvDmaCtrlHighSet - Set IDMA channel control high register
*
* DESCRIPTION:
*       Each IDMA Channel has its own unique control registers (high and low)
*       where certain IDMA modes are programmed.
*       This function writes 32bit word to IDMA control high register.
*
* INPUT:
*       chan     - DMA channel number. See MV_DMA_CHANNEL enumerator.
*       ctrlWord - Channel control word for high register.
*
* OUTPUT:
*       None.
*
* RETURN:
*       MV_OK.
*
*******************************************************************************/
MV_STATUS mvDmaCtrlHighSet(MV_U32 chan, MV_U32 ctrlWord)
{
    /* Parameter checking   */
    if (chan >= MV_IDMA_MAX_CHAN)
    {
		mvOsPrintf("mvDmaCtrlHighSet: ERR. Invalid chan num %d\n", chan);
        return MV_ERROR;
    }

    MV_REG_WRITE(IDMA_CTRL_HIGH_REG(chan), ctrlWord);
	return MV_OK;
}

/*******************************************************************************
* mvDmaTransfer - Transfer data from source to destination
* 
* DESCRIPTION:       
*       This function initiates IDMA channel, according to function parameters,
*       in order to perform DMA transaction.
*       This routine supports both chain and none chained DMA modes. 
*       To use the function in chain mode just set phyNextDesc parameter with
*       chain second descriptor address (the first one is given in other 
*       function paarameters). Otherwise (none chain mode) set it to NULL.
*       To gain maximum performance the user is asked to keep the following 
*       restrictions:
*       1) Selected engine is available (not busy).
*       1) This module does not take into consideration CPU MMU issues.
*          In order for the IDMA engine to access the appropreate source 
*          and destination, address parameters must be given in system 
*          physical mode.
*       2) This API does not take care of cache coherency issues. The source,
*          destination and in case of chain the descriptor list are assumed
*          to be cache coherent.
*       3) In case of chain mode, the API does not align the user descriptor
*          chain. Instead, the user must make sure the descriptor chain is 
*          aligned according to IDMA rquirements.
*       4) Parameters validity. For example, does size parameter exceeds 
*          maximum byte count of descriptor mode (16M or 64K).
*
* INPUT:
*       chan          - DMA channel number. See MV_DMA_CHANNEL enumerator.
*       phySrc        - Physical source address.
*       phyDst        - Physical destination address.
*       size          - The total number of bytes to transfer.
*       *pPhyNextDesc - Physical address pointer to 2nd descriptor in chain. 
*                       In case of none chain mode set it to NULL.
*
* OUTPUT:
*       None.
*
* RETURS:
*       MV_OK.
*
*******************************************************************************/
MV_STATUS mvDmaTransfer(MV_U32 chan, MV_U32 phySrc, MV_U32 phyDst, MV_U32 size, 
                                                    MV_U32 phyNextDescPtr)
{   
	/* Set byte count register			*/
	MV_REG_WRITE(IDMA_BYTE_COUNT_REG(chan), size);
	/* Set source address register		*/
	MV_REG_WRITE(IDMA_SRC_ADDR_REG(chan), phySrc);
	/* Set destination address register	*/
	MV_REG_WRITE(IDMA_DST_ADDR_REG(chan), phyDst);
	
    if (0 != phyNextDescPtr)
	{	/* Chain mode. Set next descriptor register	*/
		MV_REG_WRITE(IDMA_NEXT_DESC_PTR_REG(chan), phyNextDescPtr);
	}
	
	/* Start DMA	*/
	MV_REG_BIT_SET(IDMA_CTRL_LOW_REG(chan), ICCLR_CHAN_ENABLE);
	
    return MV_OK;
}

/*******************************************************************************
* mvDmaStateGet - Get IDMA channel status.
*
* DESCRIPTION:
*       DMA channel status can be active, stopped, paused.
*       This function retrunes the channel status.
*
* INPUT:
*       chan - IDMA channel number. See MV_DMA_CHANNEL enumerator.
*
* OUTPUT:
*       None.
*
* RETURN:
*       One of MV_STATE enumerator values.
*
*******************************************************************************/
MV_STATE mvDmaStateGet(MV_U32 chan)
{
    MV_U32  ctrlLow;

    /* Read control low register    */
    ctrlLow = MV_REG_READ(IDMA_CTRL_LOW_REG(chan));
    
    /* Leave only enable/active bits    */
    ctrlLow &= (ICCLR_CHAN_ENABLE | ICCLR_CHAN_ACTIVE);

    /* If channel is enabled and active then its running        */
    if (ctrlLow == (ICCLR_CHAN_ENABLE | ICCLR_CHAN_ACTIVE))
    {
        return MV_ACTIVE;
    }
    /* If channel is disabled but active then its paused        */
    else if (ctrlLow == ICCLR_CHAN_ACTIVE)
    {
        return MV_PAUSED;
    }
    else 
    {    
        return MV_IDLE;
    }
}

/*******************************************************************************
* mvDmaCommandSet - Set command to DMA channel
*
* DESCRIPTION:
*       DMA channel can be started, idel, paused and restarted.
*       Paused can be set only if channel is active.
*       Start can be set only if channel is idle.
*       Restart can be set only if channel is paused.
*       Stop activate the channel abort which cause the DMA to aborts in the 
*       middle of a transaction.
*
* INPUT:
*       chan    - IDMA channel number. See MV_DMA_CHANNEL enumerator.
*       command - Requested command. See MV_COMMAND enumerator.
*
* OUTPUT:
*       None.
*
* RETURN:
*       MV_ERROR if requested command can not be set.
*
*******************************************************************************/
MV_STATUS mvDmaCommandSet(MV_U32 chan, MV_COMMAND command)
{
    MV_U32  ctrlLow;
    MV_U32  dmaStatus;

    /* Read control low register    */
    ctrlLow = MV_REG_READ(IDMA_CTRL_LOW_REG(chan));

    /* Get current DMA status       */
    dmaStatus = mvDmaStateGet(chan);

    if ((command == MV_START) && (dmaStatus == MV_IDLE)) 
    {
        /* To start, set DMA channel enable bit                             */
        ctrlLow |= ICCLR_CHAN_ENABLE;
    }
    else if ((command == MV_PAUSE) && (dmaStatus == MV_ACTIVE)) 
    {
        /* To pause, reset DMA channel enable bit                           */
        ctrlLow &= ~ICCLR_CHAN_ENABLE;
    }
    else if ((command == MV_RESTART) && (dmaStatus == MV_PAUSED)) 
    {
        /* To restart, set DMA channel enable bit                           */
        ctrlLow |= ICCLR_CHAN_ENABLE;
    }
    else if (command == MV_STOP)
    {
        /* To stop, set DMA channel abort bit                               */
        ctrlLow |= ICCLR_CHANNEL_ABORT;
    }    
    else
    {
		DB(mvOsPrintf("mvDmaCommandSet: ERR. Can not set command %d in \
					   status %d\n", command, dmaStatus));
        return MV_ERROR;
    }
    
    /* Write control word to register   */
    MV_REG_WRITE(IDMA_CTRL_LOW_REG(chan), ctrlLow);

    /* If comman is stop, ensure channel is stopped                         */
    if (command == MV_STOP)
    {
        while(MV_IDLE != mvDmaStateGet(chan));
    }    

    return  MV_OK;
}

/*******************************************************************************
* mvDmaWinSet - Set DMA target address window
*
* DESCRIPTION:
*       This function sets a peripheral target (e.g. SDRAM bank0, PCI_MEM0) 
*       address window, also known as address decode window. 
*       After setting this target window, the DMA will be able to access the 
*       target within the address window. 
*
* INPUT:
*       winNum      - IDMA to target address decode window number.
*       pAddrDecWin - IDMA target window data structure.
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
MV_STATUS mvDmaWinSet(MV_U32 winNum, MV_DMA_DEC_WIN *pAddrDecWin)
{
	MV_TARGET_ATTRIB targetAttribs;
        MV_DEC_REGS decRegs;

    /* Parameter checking   */
    if (winNum >= IDMA_MAX_ADDR_DEC_WIN)
    {
		mvOsPrintf("mvDmaWinSet: ERR. Invalid win num %d\n",winNum);
        return MV_BAD_PARAM;
    }
    
    /* Check if the requested window overlapps with current windows         */
    if (MV_TRUE == dmaWinOverlapDetect(winNum, &pAddrDecWin->addrWin))
   	{
        	mvOsPrintf("mvDmaWinSet: ERR. Window %d overlap\n", winNum);
		return MV_ERROR;
	}

	/* check if address is aligned to the size */
	if(MV_IS_NOT_ALIGN(pAddrDecWin->addrWin.baseLow, pAddrDecWin->addrWin.size))
	{
		mvOsPrintf("mvDmaWinSet: Error setting IDMA window %d to "\
				   "target %s.\nAddress 0x%08x is unaligned to size 0x%x.\n",
				   winNum,
				   mvCtrlTargetNameGet(pAddrDecWin->target), 
				   pAddrDecWin->addrWin.baseLow,
				   pAddrDecWin->addrWin.size);
		return MV_ERROR;
	}


	decRegs.baseReg = MV_REG_READ(IDMA_BASE_ADDR_REG(winNum));
	decRegs.sizeReg = MV_REG_READ(IDMA_SIZE_REG(winNum));

	if (MV_OK != mvCtrlAddrDecToReg(&(pAddrDecWin->addrWin),&decRegs))
	{
			mvOsPrintf("mvDmaWinSet: mvCtrlAddrDecToReg Failed\n");
			return MV_ERROR;
	}
	
	mvCtrlAttribGet(pAddrDecWin->target, &targetAttribs);

#if defined(MV_88F5182) || defined(MV_88F5181L)
    /* See BTS Nastore  #19.*/
    /* To access Tunit SRAM from IDMA use targetId = 0x5 */
    /* To access Tunit SRAM from the CPU use targetId = 0x9 */
    if(pAddrDecWin->target == CRYPT_ENG)
        targetAttribs.targetId = 5;
#endif /* defined(MV_88F5182) || defined(MV_88F5181L) */

	/* set attributes */
	decRegs.baseReg &= ~IDMA_WIN_ATTR_MASK;
	decRegs.baseReg |= targetAttribs.attrib << IDMA_WIN_ATTR_OFFS;
	/* set target ID */
	decRegs.baseReg &= ~IDMA_WIN_TARGET_MASK;
	decRegs.baseReg |= targetAttribs.targetId << IDMA_WIN_TARGET_OFFS;
	
	/* for the safe side we disable the window before writing the new
	values */
	mvDmaWinEnable(winNum,MV_FALSE);

	MV_REG_WRITE(IDMA_BASE_ADDR_REG(winNum), decRegs.baseReg);

	/* Write to address decode Size Register                             */
	MV_REG_WRITE(IDMA_SIZE_REG(winNum), decRegs.sizeReg);

	/* Enable address decode target window                               */
	if (pAddrDecWin->enable == MV_TRUE)
	{
    	mvDmaWinEnable(winNum, MV_TRUE);
	}
    
	return MV_OK;
}

/*******************************************************************************
* mvDmaWinGet - Get dma peripheral target address window.
*
* DESCRIPTION:
*		Get IDMA peripheral target address window.
*
* INPUT:
*       winNum - IDMA to target address decode window number.
*
* OUTPUT:
*       pAddrDecWin - IDMA target window data structure.
*
* RETURN:
*       MV_ERROR if register parameters are invalid.
*
*******************************************************************************/
MV_STATUS mvDmaWinGet(MV_U32 winNum, MV_DMA_DEC_WIN *pAddrDecWin)
{

	MV_DEC_REGS decRegs;
	MV_TARGET_ATTRIB targetAttrib;

	/* Parameter checking   */
	if (winNum >= IDMA_MAX_ADDR_DEC_WIN)
	{
		mvOsPrintf("mvDmaWinGet: ERR. Invalid winNum %d\n", winNum);
		return MV_NOT_SUPPORTED;
	}
	
	decRegs.baseReg =  MV_REG_READ(IDMA_BASE_ADDR_REG(winNum));                                                                           
	decRegs.sizeReg = MV_REG_READ(IDMA_SIZE_REG(winNum));
 
	if (MV_OK != mvCtrlRegToAddrDec(&decRegs,&(pAddrDecWin->addrWin)))
	{
		mvOsPrintf("mvDmaWinGet: mvCtrlRegToAddrDec Failed \n");
		return MV_ERROR;
	}
	 
	/* attrib and targetId */
	targetAttrib.attrib = 
		(decRegs.baseReg & IDMA_WIN_ATTR_MASK) >> IDMA_WIN_ATTR_OFFS;
	targetAttrib.targetId = 
		(decRegs.baseReg & IDMA_WIN_TARGET_MASK) >> IDMA_WIN_TARGET_OFFS;
	 
	pAddrDecWin->target = mvCtrlTargetGet(&targetAttrib);
	
	/* Check if window is enabled   */
	if (~(MV_REG_READ(IDMA_BASE_ADDR_ENABLE_REG)) & (IBAER_ENABLE(winNum)))
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
* mvDmaWinEnable - Enable/disable a DMA to target address window
*
* DESCRIPTION:
*       This function enable/disable a DMA to target address window.
*       According to parameter 'enable' the routine will enable the 
*       window, thus enabling DMA accesses (before enabling the window it is 
*       tested for overlapping). Otherwise, the window will be disabled.
*
* INPUT:
*       winNum - IDMA to target address decode window number.
*       enable - Enable/disable parameter.
*
* OUTPUT:
*       N/A
*
* RETURN:
*       MV_ERROR if decode window number was wrong or enabled window overlapps.
*
*******************************************************************************/
MV_STATUS mvDmaWinEnable(MV_U32 winNum,MV_BOOL enable)
{
	MV_DMA_DEC_WIN addrDecWin;
    
	/* Parameter checking   */
	if (winNum >= IDMA_MAX_ADDR_DEC_WIN)
	{
		mvOsPrintf("mvDmaWinEnable:ERR. Invalid winNum%d\n",winNum);
		return MV_ERROR;
	}

	if (enable == MV_TRUE) 
	{   /* First check for overlap with other enabled windows				*/
		/* Get current window */
		if (MV_OK != mvDmaWinGet(winNum, &addrDecWin))
		{
			mvOsPrintf("mvDmaWinEnable:ERR. targetWinGet fail\n");
			return MV_ERROR;
		}
		/* Check for overlapping */
		if (MV_FALSE == dmaWinOverlapDetect(winNum, &(addrDecWin.addrWin)))
		{
			/* No Overlap. Enable address decode target window              */
			MV_REG_BIT_RESET(IDMA_BASE_ADDR_ENABLE_REG, IBAER_ENABLE(winNum));
		}
		else
		{   /* Overlap detected	*/
			mvOsPrintf("mvDmaWinEnable:ERR. Overlap detected\n");
			return MV_ERROR;
		}
	}
	else
	{   /* Disable address decode target window                             */
		MV_REG_BIT_SET(IDMA_BASE_ADDR_ENABLE_REG, IBAER_ENABLE(winNum));
	}
	return MV_OK;
}

/*******************************************************************************
* mvDmaWinTargetGet - Get Window number associated with target
*
* DESCRIPTION:
*
* INPUT:
*
* OUTPUT:
*
* RETURN:
*	window number
*
*******************************************************************************/
MV_U32  mvDmaWinTargetGet(MV_TARGET target)
{
	MV_DMA_DEC_WIN decWin;
	MV_U32 winNum;
	
	/* Check parameters */
	if (target >= MAX_TARGETS)
	{
		mvOsPrintf("mvDmaWinTargetGet: target %d is Illigal\n", target);
		return 0xffffffff;
	}                                                                                                                                                                                                                        
	for (winNum = 0; winNum < IDMA_MAX_ADDR_DEC_WIN ; winNum++)
	{ 
		if (mvDmaWinGet(winNum,&decWin) != MV_OK)
		{
			mvOsPrintf("mvDmaWinTargetGet: mvDmaWinGet returned error\n");
			return 0xffffffff;
		}
		if (decWin.enable == MV_TRUE)
		{
			if (decWin.target == target)
			{
				return winNum;
			}
		}
	}
	return 0xFFFFFFFF;
}

/*******************************************************************************
* mvDmaProtWinSet - Set access protection of IDMA to target window.
*
* DESCRIPTION:
*       Each IDMA channel can be configured with access attributes for each 
*       of the IDMA to target windows (address decode windows). This
*       function sets access attributes to a given window for the given channel.
*
* INPUTS:
*       chan   - IDMA channel number. See MV_DMA_CHANNEL enumerator.
*       winNum - IDMA to target address decode window number.
*       access - IDMA access rights. See MV_ACCESS_RIGHTS enumerator.
*
* OUTPUT:
*       None.
*
* RETURN:
*       MV_ERROR in case window number is invalid or access right reserved.
*
*******************************************************************************/
MV_STATUS mvDmaProtWinSet (MV_U32 chan, MV_U32 winNum, MV_ACCESS_RIGHTS access)
{    
    MV_U32 protReg;

    /* Parameter checking   */
    if ((chan >= MV_IDMA_MAX_CHAN) || (winNum >= IDMA_MAX_ADDR_DEC_WIN))
    {
		mvOsPrintf("mvDmaProtWinSet:ERR. Invalid chan number %d\n", chan);
        return MV_ERROR;
    }
	if((access == ACC_RESERVED) || (access >= MAX_ACC_RIGHTS))
	{
		mvOsPrintf("mvDmaProtWinSet:ERR. Inv access param %d\n", access);
        return MV_ERROR;
	}
    /* Read current protection register */
    protReg = MV_REG_READ(IDMA_ACCESS_PROTECT_REG(chan));
    
    /* Clear protection window field */
    protReg &= ~(ICAPR_PROT_WIN_MASK(winNum));

    /* Set new protection field value */
    protReg |= (access << (ICAPR_PROT_WIN_OFFS(winNum)));
    
    /* Write protection register back   */
    MV_REG_WRITE(IDMA_ACCESS_PROTECT_REG(chan), protReg);

	return MV_OK;
}               

/*******************************************************************************
* mvDmaOverrideSet - Set DMA target window override
*
* DESCRIPTION:
*       The address override feature enables additional address decoupling. 
*       For example, it allows the use of the same source and destination 
*       addresses while the source is targeted to one interface and 
*       destination to a second interface.
*       DMA source/destination/next descriptor addresses can be override per
*       address decode windows 1, 2 and 3 only. 
*       This function set override parameters per DMA channel. It access
*       DMA control register low.
*
* INPUT:
*       chan   - IDMA channel number. See MV_DMA_CHANNEL enumerator.
*       winNum   - Override window numver. 
*                  Note: 1) Not all windows can override. 
*                        2) Window '0' means disable override.
*       override - Type of override. See MV_DMA_OVERRIDE enumerator. 
*
* OUTPUT:
*       None.
*
* RETURN:
*       MV_BAD_PARAM in case window can not perform override.
*
*******************************************************************************/
MV_STATUS mvDmaOverrideSet(MV_U32 chan, MV_U32 winNum, MV_DMA_OVERRIDE override)
{
    MV_U32 ctrlLowReg;

    /* Parameter checking   */
    if ((chan >= MV_IDMA_MAX_CHAN) || (winNum >= IDMA_MAX_OVERRIDE_WIN))
    {
		mvOsPrintf("mvDmaOverrideSet:ERR. Invalid chan num %d\n", chan);
        return MV_ERROR;
    }
    /* Read control register */
    ctrlLowReg = MV_REG_READ(IDMA_CTRL_LOW_REG(chan));

    switch (override)
    {
        case (DMA_SRC_ADDR):
            ctrlLowReg &= ~ICCLR_OVRRD_SRC_MASK;        /* Clear SRC field  */
            ctrlLowReg |= ICCLR_OVRRD_SRC_BAR(winNum);  /* Set reg field    */
            break;
        case (DMA_DST_ADDR):
            ctrlLowReg &= ~ICCLR_OVRRD_DST_MASK;        /* Clear DST field  */
            ctrlLowReg |= ICCLR_OVRRD_DST_BAR(winNum);  /* Set reg field    */
            break;
        case (DMA_NEXT_DESC):
            ctrlLowReg &= ~ICCLR_OVRRD_NDSC_MASK;       /* Clear N_Desc field*/
            ctrlLowReg |= ICCLR_OVRRD_NDSC_BAR(winNum); /* Set reg field    */
            break;
		default:
		{
			mvOsPrintf("mvDmaOverrideSet:ERR. Inv override param%d\n",override);
			return MV_BAD_PARAM;
		}
    }
    /* Write control word back	*/
	MV_REG_WRITE(IDMA_CTRL_LOW_REG(chan), ctrlLowReg);

    return MV_OK;
}

/*******************************************************************************
* mvDmaPciRemap - Set DMA remap register for PCI address windows.
*
* DESCRIPTION:
*       The PCI interface supports 64-bit addressing. Four of the eight 
*       address windows have an upper 32-bit address register. To access the 
*       PCI bus with 64-bit addressing cycles (DAC cycles), this function 
*       assigns one (or more) of these four windows to target the PCI bus. 
*       The address generated on the PCI bus is composed of the window base 
*       address and the High Remap register.
*
* INPUT:
*       winNum   - IDMA to target address decode window number. Only 0 - 3.
*       addrHigh - upper 32-bit address.
*
* OUTPUT:
*       None.
*
* RETURN:
*       MV_BAD_PARAM if window number is not between 0 and 3.
*
*******************************************************************************/
MV_STATUS mvDmaPciRemap(MV_U32 winNum, MV_U32 addrHigh)
{
    /* Parameter checking   */
    if (winNum >= IDMA_MAX_OVERRIDE_WIN)
    {
		mvOsPrintf("mvDmaPciRemap:ERR. Invalid window num %d\n", winNum);
        	return MV_BAD_PARAM;
    }
    
	MV_REG_WRITE(IDMA_HIGH_ADDR_REMAP_REG(winNum), addrHigh);

    return MV_OK;
}

/*******************************************************************************
* dmaWinOverlapDetect - Detect DMA address windows overlapping
*
* DESCRIPTION:
*       An unpredicted behaviur is expected in case DMA address decode 
*       windows overlapps.
*       This function detects DMA address decode windows overlapping of a 
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
static MV_STATUS dmaWinOverlapDetect(MV_U32 winNum, MV_ADDR_WIN *pAddrWin)
{
    MV_U32 		baseAddrEnableReg;
    MV_U32      	winNumIndex;
    MV_DMA_DEC_WIN  	addrDecWin;

    /* Read base address enable register. Do not check disabled windows		*/
	baseAddrEnableReg = MV_REG_READ(IDMA_BASE_ADDR_ENABLE_REG);

    for (winNumIndex = 0; winNumIndex < IDMA_MAX_ADDR_DEC_WIN; winNumIndex++)
    {
		/* Do not check window itself		*/
        if (winNumIndex == winNum)
		{
			continue;
		}

		/* Do not check disabled windows	*/
		if (baseAddrEnableReg & (IBAER_ENABLE(winNumIndex)))
		{
			continue;
		}

		/* Get window parameters 	*/
		if (MV_OK != mvDmaWinGet(winNumIndex, &addrDecWin))
		{
			DB(mvOsPrintf("dmaWinOverlapDetect: ERR. TargetWinGet failed\n"));
            		return MV_ERROR;
		}

		if (MV_TRUE == ctrlWinOverlapTest(pAddrWin, &(addrDecWin.addrWin)))
		{
			return MV_TRUE;
		}        
    }
	return MV_FALSE;
}

/*******************************************************************************
* mvDmaAddrDecShow - Print the DMA address decode map.
*
* DESCRIPTION:
*		This function print the DMA address decode map.
*
* INPUT:
*       None.
*
* OUTPUT:
*       None.
*
* RETURN:
*       None.
*
*******************************************************************************/
MV_VOID mvDmaAddrDecShow(MV_VOID)
{

	MV_DMA_DEC_WIN win;
	int i;

	mvOsOutput( "\n" );
	mvOsOutput( "DMA:\n" );
	mvOsOutput( "----\n" );

	for( i = 0; i < IDMA_MAX_ADDR_DEC_WIN; i++ )
	{
		memset( &win, 0, sizeof(MV_DMA_DEC_WIN) );

		mvOsOutput( "win%d - ", i );

		if( mvDmaWinGet( i, &win ) == MV_OK )
		{
			if( win.enable )
			{
                mvOsOutput( "%s base %08x, ",
                mvCtrlTargetNameGet(win.target), win.addrWin.baseLow );
                mvOsOutput( "...." );

                mvSizePrint( win.addrWin.size );

				mvOsOutput( "\n" );
			}
			else
				mvOsOutput( "disable\n" );
		}
	}
}

MV_VOID    mvIdmaRegs(MV_U32 chan)
{
    mvOsPrintf("\t IDMA #%d Registers:\n", chan);

    mvOsPrintf("IDMA_BYTE_COUNT_REG             : 0x%X = 0x%08x\n", 
                IDMA_BYTE_COUNT_REG(chan), 
                MV_REG_READ( IDMA_BYTE_COUNT_REG(chan) ) );    

    mvOsPrintf("IDMA_SRC_ADDR_REG               : 0x%X = 0x%08x\n", 
                IDMA_SRC_ADDR_REG(chan), 
                MV_REG_READ( IDMA_SRC_ADDR_REG(chan) ) );    

    mvOsPrintf("IDMA_DST_ADDR_REG               : 0x%X = 0x%08x\n", 
                IDMA_DST_ADDR_REG(chan), 
                MV_REG_READ( IDMA_DST_ADDR_REG(chan) ) );    

    mvOsPrintf("IDMA_NEXT_DESC_PTR_REG          : 0x%X = 0x%08x\n", 
                IDMA_NEXT_DESC_PTR_REG(chan), 
                MV_REG_READ( IDMA_NEXT_DESC_PTR_REG(chan) ) );    

    mvOsPrintf("IDMA_CURR_DESC_PTR_REG          : 0x%X = 0x%08x\n", 
                IDMA_CURR_DESC_PTR_REG(chan), 
                MV_REG_READ( IDMA_CURR_DESC_PTR_REG(chan) ) );    

    mvOsPrintf("IDMA_CTRL_LOW_REG               : 0x%X = 0x%08x\n", 
                IDMA_CTRL_LOW_REG(chan), 
                MV_REG_READ( IDMA_CTRL_LOW_REG(chan) ) );    

    mvOsPrintf("IDMA_CTRL_HIGH_REG              : 0x%X = 0x%08x\n", 
                IDMA_CTRL_HIGH_REG(chan), 
                MV_REG_READ( IDMA_CTRL_HIGH_REG(chan) ) );    

    mvOsPrintf("IDMA_CAUSE_REG                  : 0x%X = 0x%08x\n", 
                IDMA_CAUSE_REG, 
                MV_REG_READ( IDMA_CAUSE_REG ) );    

    mvOsPrintf("IDMA_MASK_REG                   : 0x%X = 0x%08x\n", 
                IDMA_MASK_REG, 
                MV_REG_READ( IDMA_MASK_REG ) );    
}

#endif /* #if defined(MV_88F5181) */


