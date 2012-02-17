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
 * 
 * FILENAME:    $Workfile: rgcfg.c $ 
 * REVISION:    $Revision: 1.1.1.1 $ 
 * LAST UPDATE: $Modtime: 6/24/03 1:28p $ 
 * 
 * rgcfg.c
 *
 * DESCRIPTION:
 *		Configure the RG networking and switch
 *
 * DEPENDENCIES:   UniMAC manager DLL for UNM IOCTL access.
 *
 * FILE REVISION NUMBER:
 *
 *******************************************************************************/
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "rgcfg.h"
#include "mv_unimac.h"

#ifndef GT_NUM_OF_SWITCH_PHYS
#define GT_NUM_OF_SWITCH_PHYS 5
#endif

#ifndef GT_NUM_OF_SWITCH_PORTS
#define GT_NUM_OF_SWITCH_PORTS 7
#endif

/* #define RGCFG_DEBUG */

/* general */
#define ZERO(x)   do { memset(x, 0, sizeof(x)); } while (0)
#define PZERO(px) do { memset(px, 0, sizeof(*px)); } while (0)

/* user network configuration */
#define NET_CONF_FULLPATH "/etc/config/unimac.conf"
static unsigned char macs[MV_UNM_MAX_VID][GT_ETHERNET_HEADER_SIZE];
static unsigned char vlan_names[MV_UNM_MAX_VID][MAX_VLAN_NAME];
static MV_UNM_CONFIG netconf;
static bool ReadUnimacConf(void);
static void printUnimacConf(void);
static bool IsUnmInitialized(void);

/* continuous multi-sz string */
static char* SzNext(char *tszMulti);

/* switch information */
typedef struct {
  UINT offset;
  char* name;
} regParam;

static bool getReg(UINT regOffset, UINT* data);
static bool setReg(UINT regOffset, UINT data);
static void ReadReg(UINT regOffset);
static void WriteReg(UINT regOffset, UINT data);

static regParam phyRegs[] = {
  { 0,  "PHY Control"},
  { 1,  "PHY Status"},
  { 2,  "PHY Identifier 0"},
  { 3,  "PHY Identifier 1"},
  { 4,  "Auto-Neg Advertisement"},
  { 5,  "Link Partner Ability"},
  { 6,  "Auto-Neg Expansion"},
  { 7,  "Next Page Transmit"},
  { 8,  "Link Partner Next Page"},
  /* reserved 9-15 */
  { 16, "PHY Specific Control"},
  { 17, "PHY Specific tatus"},
  { 18, "PHY Interrupt Enable"},
  { 19, "PHY Interrupt status"},
  { 20, "Interrupt Port Summary"},
  { 21, "Receive Error Counter"},
  { 22, "LED Parallel Select"},
  { 23, "LED Stream Select"},
  { 24, "LED Control"}, 
  /* reserved 25-31 */
};
static regParam portRegs[] = {
  { 0,  "Port Status"},
  /* reserved 1-2 */
  { 3,  "Switch Identifier"},
  { 4,  "Port Control"},
  /* reserved 5 */
  { 6,  "Port Based VLAN Map"},
  { 7,  "Default VID & Priority"},
  /* reserved 8-15 */
  { 16, "Rx Stat Counter"},
  { 17, "Tx Stat Counter"},
  /* reserved 18-26 */
  { 27, "Queue Counter"},
  /* reserved 28-31 */
};
static regParam globalRegs[] = {
  { 0,  "Global Status"},
  { 1,  "Switch MAC bytes 0:1"},
  { 2,  "Switch MAC bytes 2:3"},
  { 3,  "Switch MAC bytes 4:5"},
  { 4,  "Global Control"},
  /* reserved 5-9 */
  { 10, "ATU Control"},
  { 11, "ATU Operation"},
  { 12, "ATU Data"},
  { 13, "ATU MAC bytes 0:1"},
  { 14, "ATU MAC bytes 2:3"},
  { 15, "ATU MAC bytes 4:5"},
  { 16, "IP-PRI Mapping 0"},
  { 17, "IP-PRI Mapping 1"},
  { 18, "IP-PRI Mapping 2"},
  { 19, "IP-PRI Mapping 3"},
  { 20, "IP-PRI Mapping 4"},
  { 21, "IP-PRI Mapping 5"},
  { 22, "IP-PRI Mapping 6"},
  { 23, "IP-PRI Mapping 7"},
  { 24, "IEEE-PRI Mapping"},
  { 25, "QC Threshold 1"},
  { 26, "QC Threshold 2"},
  { 27, "Total Free Counter"},
  { 28, "QC Threshold 3"},
  /* reserved 29-31 */
};

static bool getSwitchInformation(char** name, UCHAR* revision, UCHAR* devnum,
				 UCHAR* enabled, UCHAR* baseaddr,
				 UCHAR* nports, UCHAR* cpuport);
static bool getSwitchReg(UCHAR phyAddr, UCHAR regOffset, UINT* data);
static bool setSwitchReg(UCHAR phyAddr, UCHAR regOffset, UINT data);
static void showSwitchRegs(UINT phyMap, UINT regMap, UCHAR baseAddr);
static void ShowSwitchInformation(void);
static void ReadSwitchReg(UCHAR phyAddr, UCHAR regOffset);
static void WriteSwitchReg(UCHAR phyAddr, UCHAR regOffset, UINT data);
static void ShowAllSwitchRegs(UINT phyMap, UINT regMap);

/* kernel driver access */
static bool DeviceIoControl(DWORD  hDevice, 
			    DWORD  code,
			    UCHAR *pInBuff,
			    UINT   inLen,
			    UCHAR *pOutBuff,
			    UINT   outLen,
			    DWORD *pdwBytesWritten,
			    DWORD *pUnused);
static DWORD hDevice = 0, dwBytesTransferred;
static GT_IOCTL_PARAM ioctlParam, OutIoctlParam;
static bool  bRc;
  

void showUsage(void)
{
  printf("Residential Gateway Configuration Program, Marvell 2003\n");
  printf("Usage: rgcfg [options]\n");
  printf("Options:\n");
  printf("    -h                                Display help message\n");
  printf("  Network Configuration:\n");
  printf("    -nd                               Display current network configuration\n");
  printf("    -np port [vlanName]               Move port to interface (VLAN),\n");
  printf("                                        or free the port if no VLAN is given\n");
  printf("  Register Access:\n");
  printf("    -rr regOffset                     Read register (hex params)\n");
  printf("    -rw regOffset data                Write register (hex params)\n");
  printf("  Switch Configuration:\n");
  printf("    -si                               Show switch information\n");
  printf("    -sr phyAddr regOffset             Read switch register (hex params)\n");
  printf("    -sw phyAddr regOffset data        Write switch register (hex params)\n");
  printf("\n");
}


int main(int argc, char* argv[])
{
  if (argc < 2) {
    showUsage();
    return 1;
  }
  
  if (strcmp(argv[1], "-si") == 0) {
    ShowSwitchInformation();
  }

  else if (strcmp(argv[1], "-sr") == 0) {
    UCHAR phyAddr, regOffset;

    if (argc != 4) {
      showUsage();
      return 1;
    }
    phyAddr = (UCHAR)strtoul(argv[2], NULL, 16);
    regOffset = (UCHAR)strtoul(argv[3], NULL, 16);
    ReadSwitchReg(phyAddr, regOffset);
  }

  else if (strcmp(argv[1], "-sw") == 0) {
    UCHAR phyAddr, regOffset;
    UINT data;

    if (argc != 5) {
      showUsage();
      return 1;
    }

    phyAddr = (UCHAR)strtoul(argv[2], NULL, 16);
    regOffset = (UCHAR)strtoul(argv[3], NULL, 16);
    data = strtoul(argv[4], NULL, 16);
    WriteSwitchReg(phyAddr, regOffset, data);
  } 
   
  else if (strcmp(argv[1], "-i") == 0) {
    if (IsUnmInitialized()) {
      printf("Error: rgcfg already initialized\n");
      return 0;
    }

    /* read and parse user net config */
    bRc = ReadUnimacConf();
    if (!bRc) {
      return 1;
    }
    printUnimacConf();

    /* update the UniMAC manager */
    sleep(2);
    memcpy(&(ioctlParam.FirstParam.netconf), &netconf, sizeof(netconf)); 
    memcpy((ioctlParam.SecondParam.vlan_names), vlan_names, sizeof(vlan_names));
    memcpy((ioctlParam.ThirdParam.macs), macs, sizeof(macs));
    bRc = DeviceIoControl(hDevice, 
			  IOCTL_UNM_INIT,
			  (char*)&ioctlParam, sizeof(ioctlParam), NULL, 0,
			  &dwBytesTransferred,
			  NULL );
    if (!bRc) {
      printf("Error: Cannot initialize user network configuration\n");
    }

    /* show switch information */
    ShowSwitchInformation();

    printf("Init: User network configuration initialized\n"); 
  }

  else if (strcmp(argv[1], "-nd") == 0) {
    WCHAR *szIter, OutBuf[128];
    DWORD i, port;

    if (!IsUnmInitialized()) {
      printf("Error: rgcfg not initialized yet\n");
      return 0;
    }

    /* get and display interface names */
    bRc = DeviceIoControl(hDevice, 
			  IOCTL_UNM_GET_VLAN_NAMES,
			  NULL, 0,
			  OutBuf, sizeof(OutBuf),
			  &dwBytesTransferred,
			  NULL );
    if (!bRc) {
      printf("Error: Cannot get interface names\n");
    }

    for (i = 1, szIter = OutBuf;
	 i <= GT_NUM_OF_SWITCH_PORTS && szIter && *szIter;
	 i++, szIter = SzNext(szIter)) {
      bRc = DeviceIoControl(hDevice, 
			    IOCTL_UNM_GET_VLAN_PARMS,
			    szIter, strlen(szIter) + 1, 
			    (char*)&OutIoctlParam, sizeof(OutIoctlParam), 
			    &dwBytesTransferred,
			    NULL);
      if (!bRc) {
	printf("Error: Cannot get parameters of interface %s\n", szIter);
	return 1;
      }
      else {
	printf("VLAN %d Parameters:\n", i);
	printf("\tName        = %s\n", szIter);
	printf("\tMAC Address = %02x:%02x:%02x:%02x:%02x:%02x\n",	
	      OutIoctlParam.FirstParam.etherAddr.arEther[0],
	      OutIoctlParam.FirstParam.etherAddr.arEther[1],
	      OutIoctlParam.FirstParam.etherAddr.arEther[2],
	      OutIoctlParam.FirstParam.etherAddr.arEther[3],
	      OutIoctlParam.FirstParam.etherAddr.arEther[4],
	      OutIoctlParam.FirstParam.etherAddr.arEther[5]);
      }
    } 
    printf("Total number of VLANs = %d\n\n", i - 1);

    /* get and display port configuration */
    printf("Switch Port Association:\n");
    for (port = 0; port < GT_NUM_OF_SWITCH_PORTS; port++) {
      if (port == GT_CPU_SWITCH_PORT) {
	continue;
      }
      ioctlParam.FirstParam.port = port;
      bRc = DeviceIoControl(hDevice,
			    IOCTL_UNM_GET_PORT_VLAN,
			    (char*)&ioctlParam, sizeof(ioctlParam), 
			    (char*)&OutIoctlParam, sizeof(ioctlParam),
			    &dwBytesTransferred,
			    NULL);
      if (!bRc) {
	printf("Error: Cannot get the interface of port %d\n", port);
      }
      else {
	if ((dwBytesTransferred != 0) && *OutIoctlParam.SecondParam.szVlanName) {
	  printf("\tPort %d in %s\n",
		 port, OutIoctlParam.SecondParam.szVlanName);
	}
	else {
	  printf("\tPort %d (free)\n", port);
	}
      }
    }
    printf("Total number of ports = %d\n\n", port - 1);
  }

  else if (strcmp(argv[1], "-np") == 0) {
    DWORD i, port;

    if (!IsUnmInitialized()) {
      printf("Error: rgcfg not initialized yet\n");
      return 0;
    }

    if (argc < 3 || argc > 4) {
      showUsage();
      return 1;
    }

    port = strtoul(argv[2], NULL, 16);
    for (i = 0; i < GT_NUM_OF_SWITCH_PORTS; i++) {
      if (port == i && i != GT_CPU_SWITCH_PORT) {
	break; /* found in valid range */
      }
    }
    if (i == GT_NUM_OF_SWITCH_PORTS) {
      printf("Error: Port number %d is invalid (valid range is 0..%d, except %d)\n",
	     port, GT_NUM_OF_SWITCH_PORTS - 1, GT_CPU_SWITCH_PORT);
      return 1;
    }
    ioctlParam.FirstParam.port = port;

    /* disassociate the port from its current VLAN */
    if (argc >= 3) {
      bRc = DeviceIoControl(hDevice, 
			    IOCTL_UNM_DISASSSOC_PORT,
			    (char*)&ioctlParam, sizeof(ioctlParam),
			    NULL, 0,
			    &dwBytesTransferred,
			    NULL );  
      if (!bRc) {
	printf("Error: Cannot disassociate port %d\n", port);
	return 1;
      }
    }
    /* if VLAN name is given, associate the port with a new VLAN */
    if (argc == 4) {
      memcpy(ioctlParam.SecondParam.szVlanName,argv[3], strlen(argv[3]) + 1);
      bRc = DeviceIoControl(hDevice, 
			    IOCTL_UNM_ASSOC_PORT,
			    (char*)&ioctlParam, sizeof(ioctlParam),
			    NULL, 0,
			    &dwBytesTransferred,
			    NULL );
      if (!bRc) {
	printf("Error: Cannot associate port %d with interface %s\n",
	       port, argv[3]);
	return 1;
      }
    }

    printf("Port %d is now ", port);
    if (argv[3] != 0) {
      printf("associated with interface %s\n", argv[3]);
    }
    else {
      printf("free\n");
    }
  }

  else if (strcmp(argv[1], "-rr") == 0) {
    UINT regOffset;

    if (argc != 3) {
      showUsage();
      return 1;
    }
    regOffset = strtoul(argv[2], NULL, 16);
    ReadReg(regOffset);
  }

  else if (strcmp(argv[1], "-rw") == 0) {
    UINT regOffset, data;

    if (argc != 4) {
      showUsage();
      return 1;
    }
    regOffset = strtoul(argv[2], NULL, 16);
    data = strtoul(argv[3], NULL, 16);
    WriteReg(regOffset, data);
  }

  else {
    showUsage();
  }
  
  return 0;
}


static bool DeviceIoControl(DWORD  hDevice, 
			    DWORD  code,
			    UCHAR *pInBuff,
			    UINT   inLen,
			    UCHAR *pOutBuff,
			    UINT   outLen,
			    DWORD *pdwBytesWritten,
			    DWORD *pUnused)
{
  DWORD dwresbuffer[1024/sizeof(DWORD)];
  DWORD dwcmdbuffer[1024/sizeof(DWORD)];  
  int readCtr = 0;
  FILE *fp;
  
  if (pdwBytesWritten) {
    *pdwBytesWritten = 0;
  }
  if (pOutBuff) {
    *pOutBuff = '\0';
  }

  /*
   * step 1: write command into IO file
   */
  
  /* prepare the command string */
  ZERO(dwcmdbuffer);

  dwcmdbuffer[0] = code;
  dwcmdbuffer[1] = inLen;
  if (inLen > 0) {
    memcpy(&dwcmdbuffer[2], pInBuff, inLen);
  }
#ifdef RGCFG_DEBUG
  {
    int i;
    printf("DeviceIoControl command:\n");
    printf("\tcode  = %d\n", dwcmdbuffer[0]);
    printf("\tinLen = %d\n", dwcmdbuffer[1]);
    printf("\tdata  = ");
    for (i = 0; i < dwcmdbuffer[1]; i++) {
      printf("%02x ", ((char*)&dwcmdbuffer[2])[i]);
    }
    printf("\n");
  }
#endif
  
  /* open the IO file and write the command */
  fp = fopen(RG_IO_FULLPATH, "w");
  if (!fp) {
    printf("Error: Cannot write into IO file %s", RG_IO_FULLPATH);
    return false;
  }
  fwrite(&dwcmdbuffer, 1, sizeof(dwcmdbuffer), fp);
  fclose(fp);
  
  /*
   * step 2: read result from IO file
   */
  
  /* prepare the result string */
  ZERO(dwresbuffer);
  
  /* open the IO file and read the result */
  fp = fopen(RG_IO_FULLPATH, "r");
  if (!fp) {
    printf("Error: Cannot read from IO file %s", RG_IO_FULLPATH);
    return false; 
  }
  readCtr = fread(&dwresbuffer, 1, sizeof(dwresbuffer), fp);
  fclose(fp);
  
  if (readCtr == 0) {
    printf("Error: Got no result from IO file\n");
    return false;
  }

  /* get result length */
  if (dwresbuffer[0] != (readCtr - sizeof(DWORD))) {
    printf("Error: Result length mismatch\n");
    return false;
  }
  if (dwresbuffer[0] == 0xFFFFFFFF) {
    printf("Error: Got error result from IO file\n");
    return false;
  }
  *pdwBytesWritten = dwresbuffer[0];
  
  /* get result data */
  if ((dwresbuffer[0] > 0) && (outLen < readCtr - sizeof(DWORD))) {
    printf("Error: Output buffer is too short for result\n");
    return false;
  }

#ifdef RGCFG_DEBUG
  {
    int i;
    printf("DeviceIoControl result:\n");
    printf("\toutLen = %d\n", dwresbuffer[0]);
    printf("\tdata   = ");
    for (i = 0; i < dwresbuffer[0]; i++) {
      printf("%02x ", ((char*)&dwresbuffer[2])[i]);
    }
    printf("\n");
  }
#endif
  
  memcpy(pOutBuff, &dwresbuffer[1], dwresbuffer[0]);

  return true;
} 


/* ReadUnimacConf - Read the network configuration from the user's net config file
 */
static bool ReadUnimacConf(void)
{
  char buf[512], *token, *delim = "\t\r\n ,=:"; 
  int num_of_vlans = 0, vid = 0, bytes = 0, line = 0;
  int port, i;
  unsigned char macbyte;
  bool done = false;
  FILE* fp;

  ZERO(macs);
  ZERO(vlan_names);
  PZERO(&netconf);

  /* open netconfig file */
  fp = fopen(NET_CONF_FULLPATH, "r");
  if (!fp) {
    printf("Cannot read user config from %s, using default instead.\n",
	   NET_CONF_FULLPATH);

    num_of_vlans = 3;

    strcpy(vlan_names[1], "WAN");
    strcpy(vlan_names[2], "LAN");
    strcpy(vlan_names[3], "DMZ");

    netconf.vidOfPort[0] = 1; /* WAN = port 0*/
    netconf.vidOfPort[1] = 2; /* LAN = ports 1, 2, 3 */
    netconf.vidOfPort[2] = 2;
    netconf.vidOfPort[3] = 2;
    netconf.vidOfPort[4] = 3; /* DMZ = port 4*/

    macs[1][0] = '\x00'; macs[1][1] = '\x00'; macs[1][2] = '\x20';
    macs[1][3] = '\x20'; macs[1][4] = '\x20'; macs[1][5] = '\x01';
    macs[2][0] = '\x00'; macs[2][1] = '\x00'; macs[2][2] = '\x20';
    macs[2][3] = '\x20'; macs[2][4] = '\x20'; macs[2][5] = '\x02';
    macs[3][0] = '\x00'; macs[3][1] = '\x00'; macs[3][2] = '\x20';
    macs[3][3] = '\x20'; macs[3][4] = '\x20'; macs[3][5] = '\x03';

    return true;
  }

#ifdef RGCFG_DEBUG
  printf("Parsing user net config file %s\n", NET_CONF_FULLPATH);
#endif
  
  /* parse netconfig file */
  while ((fgets(buf, sizeof(buf), fp) != NULL) && !done) {
    line++;
    if (*buf == '\0' || *buf == '#') {
      continue; /* skip empty and comment lines */
    }
#ifdef RGCFG_DEBUG
    printf("line %d: %s\n", line, buf);
#endif

    /* find next token */
    token = strtok(buf, delim);
    if (!token) {
      continue; /* skip empty tokens */
    }

    /* line format: VLANS = <numOfVlans> */
    if (strcmp(token, "VLANS") == 0) {
      if (num_of_vlans != 0) {
	printf("Error (line %d): number of VLANs already defined (%d)\n",
	       line, num_of_vlans);
	done = true;
	break;
      }
      token = strtok(NULL, delim);
      num_of_vlans = strtol(token, NULL, 16);
      if (num_of_vlans < 1 || num_of_vlans > GT_NUM_OF_SWITCH_PORTS - 1) {
	printf("Error (line %d): Invalid number of VLANs %d (should be 1..%d)\n",
	       line, num_of_vlans, GT_NUM_OF_SWITCH_PORTS - 1);
	done = true;
	break;
      }
#ifdef RGCFG_DEBUG
      printf("number of VLANs = %d\n", num_of_vlans);
#endif
    }

    /* line format: VLAN = <vlanName> */
    else if (strcmp(token, "VLAN") == 0) {
      if (num_of_vlans == 0) {
	printf("Error (line %d): number of VLANs not defined\n", line);
	done = true;
	break;
      }
      token = strtok(NULL, delim);
      vid++; /* ID of new VLAN */
      if (vid > num_of_vlans) {
	printf("Error (line %d): Too many VLANs (should be no more than %d)\n",
	       line, num_of_vlans);
	done = true;
	break;
      }
      if (strcmp(vlan_names[vid], "") != 0) {
	printf("Error (line %d): VLAN %d already has a name (%s)\n",
	       line, vid, vlan_names[vid]);
	done = true;
	break;
      }
      for (i = 0; (i < vid) && !done; i++) {
	if (strcmp(token, vlan_names[i]) == 0) {
	  printf("Error (line %d): The name %s is already used for VLAN %d\n",
		 line, vlan_names[i], i);
	  done = true;
	  break;
	}
      }
      strcpy(vlan_names[vid], token);
#ifdef RGCFG_DEBUG
      printf("VLAN %d: vlanName = %s\n", vid, token);
#endif
    }

    /* line format: PORTS = <port1> <port2> ... <portN> */
    else if (strcmp(token, "PORTS") == 0) {
      if (strcmp(vlan_names[vid], "") == 0) {
	printf("Error (line %d): VLAN %d has no name yet\n", line, vid);
	done = true;
	break;
      }
      while (((token = strtok(NULL, delim)) != NULL) && !done) { 
	port = strtol(token, NULL, 16);
	if (port < 0 || port > GT_NUM_OF_SWITCH_PORTS || port == GT_CPU_SWITCH_PORT) {
	  printf("Error (line %d): Invalid port number %d (should be in 0..%d, except %d)\n",
		 line, port, GT_NUM_OF_SWITCH_PORTS - 1, GT_CPU_SWITCH_PORT);
	  done = true;
	  break;
	}
	if (netconf.vidOfPort[port] != 0) {
	  printf("Error (line %d): port %d already assigned to VLAN %d (%s)\n",
		 line, port, netconf.vidOfPort[port], vlan_names[netconf.vidOfPort[port]]);
	  done = true;
	  break;
	}
	netconf.vidOfPort[port] = vid;
#ifdef RGCFG_DEBUG
	printf("VLAN %d (%s): Added port %d\n", vid, vlan_names[vid], port);
#endif
      }
    }

    /* line format: MAC = <aa:bb:cc:dd:ee:ff>  */
    else if (strcmp(token, "MAC") == 0) {
      if (strcmp(vlan_names[vid], "") == 0) {
	printf("Error (line %d): VLAN %d has no name yet\n", line, vid);
	done = true;
	break;
      }
      if (macs[vid][0] != 0) {
	printf("Error (line %d): VLAN %d (%s) already has MAC address\n",
	       line, vid, vlan_names[vid]);
	done = true;
	break;
      }
      bytes = 0;
      while (((token = strtok(NULL, delim)) != NULL) && (bytes < 6) && !done) {
	macbyte = strtol(token, NULL, 16);
	macs[vid][bytes] = macbyte;
	bytes++;
      }
#ifdef RGCFG_DEBUG
      printf("VLAN %d (%s): MAC address = %02x:%02x:%02x:%02x:%02x:%02x\n",
	     vid, vlan_names[vid],
	     macs[vid][0], macs[vid][1], macs[vid][2],
	     macs[vid][3], macs[vid][4], macs[vid][5]);
#endif
    }

    /* line format: ??? */
    else {
      printf("Error (line %d): Unknown token (%s), line ignored\n",
	     line, token);
    }
  }

  fclose(fp);
  return !done;
}


static void printUnimacConf(void)
{
  int vid, port;

  printf("User's Net Config:\n");
  for (vid = 0; vid < MV_UNM_MAX_VID; vid++) {
    if (strcmp(vlan_names[vid], "") != 0) {
      printf("\tVLAN %d (%s):\n", vid, vlan_names[vid]);
      printf("\t\tMAC address = %02x:%02x:%02x:%02x:%02x:%02x\n",
	     macs[vid][0], macs[vid][1], macs[vid][2],
	     macs[vid][3], macs[vid][4], macs[vid][5]);
      printf("\t\tPort Set = { ");
      for (port = 0; port < GT_NUM_OF_SWITCH_PORTS; port++) {
	if (netconf.vidOfPort[port] == vid) {
	  printf("%d, ", port);
	}
      }
      printf("%d }\n", GT_CPU_SWITCH_PORT);
    }
  }
}


/* SzNext - Return a pointer to the next component of a continuous multi_sz
 * string.
 */
static char* SzNext(char* tszMulti)
{
  char* p = tszMulti;

  if (p) {
    p += strlen(p) + 1; /* find the beginning of the next string */
  }
  return p;
}


static bool IsUnmInitialized(void)
{
  bool initialized;

  bRc = DeviceIoControl(hDevice,
			IOCTL_UNM_STATUS,
			NULL, 0,
			(char*)&initialized, sizeof(initialized),
			&dwBytesTransferred,
			NULL);
  if (!bRc || dwBytesTransferred != sizeof(bool)) {
    printf("Error: Cannot get Unimac status\n");
    return false;
  }
  return initialized;
}


static void ShowSwitchInformation(void)
{
  char* name;
  UCHAR revision, devnum, enabled, baseaddr, nports, cpuport;

  bRc = getSwitchInformation(&name, &revision, &devnum, &enabled,
			     &baseaddr, &nports, &cpuport);
  if (!bRc) {
    printf("Error: Cannot get switch information\n");
    return;
  }
  
  printf("Internal Switch Information:\n");
  printf("\tDevice Name      = %s\n", name);
  printf("\tRevision         = %d\n", revision);
  printf("\tDevice Number    = %d\n", devnum);
  printf("\tDevice Enabled   = %s\n", enabled? "Yes" : "No");
  printf("\tBase Reg Address = %d\n", baseaddr);
  printf("\tNumber of Ports  = %d\n", nports);
  printf("\tCPU Port         = %d\n", cpuport);
  printf("\n");
}



static bool getSwitchInformation(char** name, UCHAR* revision, UCHAR* devnum,
				 UCHAR* enabled, UCHAR* baseaddr,
				 UCHAR* nports, UCHAR* cpuport)
{
  bRc = DeviceIoControl(hDevice, 
			IOCTL_sysConfig,
			NULL, 0,
			(char*)&ioctlParam, sizeof(ioctlParam),
			&dwBytesTransferred,
			NULL );
  if (!bRc) {
    return false;
  }
  
  if (name) {
    *name =
      (ioctlParam.FirstParam.qd_dev.deviceId == 0x021)? "88E6021":
      (ioctlParam.FirstParam.qd_dev.deviceId == 0x051)? "88E6051":
      (ioctlParam.FirstParam.qd_dev.deviceId == 0x052)? "88E6052":
      (ioctlParam.FirstParam.qd_dev.deviceId == 0x060)? "88E6060":
      (ioctlParam.FirstParam.qd_dev.deviceId == 0x151)? "88E6061":
      (ioctlParam.FirstParam.qd_dev.deviceId == 0x152)? "88E6062":
      (ioctlParam.FirstParam.qd_dev.deviceId == 0x153)? "88E6063":
      (ioctlParam.FirstParam.qd_dev.deviceId == 0xF51)? "88E6317":
      (ioctlParam.FirstParam.qd_dev.deviceId == 0xF53)? "88E6318":
      (ioctlParam.FirstParam.qd_dev.deviceId == 0xF91)? "88E6208":
      (ioctlParam.FirstParam.qd_dev.deviceId == 0xF93)? "88E6218":
      "(unknown)";
  }
  if (revision) {
    *revision = *(UCHAR*)&ioctlParam.FirstParam.qd_dev.revision;
  }
  if (devnum) {
    *devnum   = *(UCHAR*)&ioctlParam.FirstParam.qd_dev.devNum;
  }
  if (enabled) {
    *enabled  = *(UCHAR*)&ioctlParam.FirstParam.qd_dev.devEnabled;
  }
  if (baseaddr) {
    *baseaddr = *(UCHAR*)&ioctlParam.FirstParam.qd_dev.baseRegAddr;
  }
  if (nports) {
    *nports   = *(UCHAR*)&ioctlParam.FirstParam.qd_dev.numOfPorts;
  }
  if (cpuport) {
    *cpuport  = *(UCHAR*)&ioctlParam.FirstParam.qd_dev.cpuPortNum;
  }

  return true;
}


static bool getReg(UINT regOffset, UINT* data)
{
  ioctlParam.FirstParam.u32Data = regOffset;
  
  /* read register */
  bRc = DeviceIoControl(hDevice,
			IOCTL_UNM_READ_REG,
			(char*)&ioctlParam, sizeof(ioctlParam), 
			(char*)&OutIoctlParam, sizeof(ioctlParam),
			&dwBytesTransferred,
			NULL);
  if (!bRc) {
    return false;
  }

  *data = OutIoctlParam.FirstParam.u32Data;
  return true;
}


static bool setReg(UINT regOffset, UINT data)
{
  ioctlParam.FirstParam.u32Data = regOffset;
  ioctlParam.SecondParam.u32Data = data; 
  
  /* write switch register */
  bRc = DeviceIoControl(hDevice, 
			IOCTL_UNM_WRITE_REG,
			(char*)&ioctlParam, sizeof(ioctlParam),
			NULL, 0,
			&dwBytesTransferred,
			NULL);
  return bRc;
}


static bool getSwitchReg(UCHAR phyAddr, UCHAR regOffset, UINT* data)
{
  ioctlParam.FirstParam.u32Data = (UINT)phyAddr;
  ioctlParam.SecondParam.u32Data = (UINT)regOffset;
  
  /* read switch register */
  bRc = DeviceIoControl(hDevice,
			IOCTL_gsysReadMiiReg,
			(char*)&ioctlParam, sizeof(ioctlParam), 
			(char*)&OutIoctlParam, sizeof(ioctlParam),
			&dwBytesTransferred,
			NULL);
  if (!bRc) {
    return false;
  }

  *data = OutIoctlParam.FirstParam.u32Data;
  return true;
}


static bool setSwitchReg(UCHAR phyAddr, UCHAR regOffset, UINT data)
{
  ioctlParam.FirstParam.u32Data = (UINT)phyAddr;
  ioctlParam.SecondParam.u32Data = (UINT)regOffset;
  ioctlParam.ThirdParam.u16Data = data;
  
  /* write switch register */
  bRc = DeviceIoControl(hDevice, 
			IOCTL_gsysWriteMiiReg,
			(char*)&ioctlParam, sizeof(ioctlParam),
			NULL, 0,
			&dwBytesTransferred,
			NULL);
  return bRc;
}


static void showSwitchRegs(UINT phyMap, UINT regMap, UCHAR baseAddr)
{
  UINT i, j, data;

  if (phyMap & 0x8000) {
    printf("Global Registers:          \n");
    for (i = 0; i < sizeof(globalRegs)/sizeof(globalRegs[0]); i++) {
      if ((regMap & (1 << globalRegs[i].offset)) == 0) {
	continue;
      }
      printf("%22s (%2d)", globalRegs[i].name, globalRegs[i].offset);
      bRc = getSwitchReg(baseAddr + 15, globalRegs[i].offset, &data);
      if (!bRc) {
	printf("  ????");
      }
      else {
	printf("  %04x", data);
      }
      printf("\n");
    }
    printf("\n");
  }
  if (phyMap & 0x1f) {
    printf("Phy Registers:             ");
    for (j = 0; j < GT_NUM_OF_SWITCH_PHYS; j++) {
      printf("  %4d", baseAddr + j);
    }
    printf("\n");
    for (i = 0; i < sizeof(phyRegs)/sizeof(phyRegs[0]); i++) {
      if ((regMap & (1 << phyRegs[i].offset)) == 0) {
	continue;
      }
      printf("%22s (%2d)", phyRegs[i].name, phyRegs[i].offset);
      for (j = 0; j < GT_NUM_OF_SWITCH_PHYS; j++) {
	if ((phyMap & (1 << j)) == 0) {
	  printf("      ");
	  continue;
	}
	bRc = getSwitchReg(baseAddr + j, phyRegs[i].offset, &data);
	if (!bRc) {
	  printf("  ????");
	}
	else {
	  printf("  %04x", data);
	}
      }
      printf("\n");
    }
    printf("\n");
  }
  if (phyMap & 0x7f00) {
    printf("Port Registers:            ");
    for (j = 0; j < GT_NUM_OF_SWITCH_PORTS; j++) {
      printf("  %4d", baseAddr + j + 8);
    }
    printf("\n");
    for (i = 0; i < sizeof(portRegs)/sizeof(portRegs[0]); i++) {
      if ((regMap & (1 << portRegs[i].offset)) == 0) {
	continue;
      }
      printf("%22s (%2d)", portRegs[i].name, portRegs[i].offset);
      for (j = 0; j < GT_NUM_OF_SWITCH_PORTS; j++) {
	if ((phyMap & (1 << (j + 8))) == 0) {
	  printf("      ");
	  continue;
	}
	bRc = getSwitchReg(baseAddr + j + 8, portRegs[i].offset, &data);
	if (!bRc) {
	  printf("  ????");
	}
	else {
	  printf("  %04x", data);
	}
      }
      printf("\n");
    }
    printf("\n");
  }
}


static void ReadReg(UINT regOffset)
{
  UINT data;

  bRc = getReg(regOffset, &data);
  if (!bRc) {
    printf("Error: Cannot read register\n");
  }
  else {
    printf("Read: reg 0x%x == 0x%08x\n", regOffset, data);
  }
  printf("\n");
}


static void WriteReg(UINT regOffset, UINT data)
{
  bRc = setReg(regOffset, data);
  if (!bRc) {
    printf("Error: Cannot write register\n");
  }
  else {
    printf("Write: reg 0x%x = 0x%08x\n", regOffset, data);
  }
  printf("\n");
}


static void ReadSwitchReg(UCHAR phyAddr, UCHAR regOffset)
{
  UINT data;

  bRc = getSwitchReg(phyAddr, regOffset, &data);
  if (!bRc) {
    printf("Error: Cannot read switch register\n");
  }
  else {
    printf("Switch read: phy %d reg %d == 0x%04x\n",
	   phyAddr, regOffset, data);
  }
  printf("\n");
}


static void WriteSwitchReg(UCHAR phyAddr, UCHAR regOffset, UINT data)
{
  bRc = setSwitchReg(phyAddr, regOffset, data);
  if (!bRc) {
    printf("Error: Cannot write switch register\n");
  }
  else {
    printf("Switch write: phy %d reg %d = 0x%04x\n",
	   phyAddr, regOffset, data);
  }
  printf("\n");
}
