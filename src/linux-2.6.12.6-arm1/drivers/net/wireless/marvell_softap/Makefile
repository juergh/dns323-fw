###############################################################################
#
# Name:        Makefile.Kernel26
# Project:     Linux SoftAP for Marvell CB35 cardbus cards
# Version:     $Revision: 1.1.1.1 $
# Date:        $Date: 2009/08/19 08:02:21 $
# Author:      Ralph Roesler (rroesler@syskonnect.de)
# Purpose:     builds the object files if located in kernel tree
#
###############################################################################

###############################################################################
#
#	(C)Copyright 2004-2005 SysKonnect GmbH.
#	(C)Copyright 2004-2005 Marvell.
#
#	This program is free software; you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
# 	the Free Software Foundation; either version 2 of the License, or
#	(at your option) any later version.
#
#	The information in this file is provided "AS IS" without warranty.
# 
###############################################################################

mwl-objs	:= \
		driver/mwl_debug.o		\
		driver/mwl_descriptors.o	\
		driver/mwl_download.o		\
		driver/mwl_ethtool.o		\
		driver/mwl_hostcmd.o		\
		driver/mwl_hotplug.o		\
		driver/mwl_main.o		\
		driver/mwl_wlanext.o		\
		driver/mwl_xmitrecv.o

wlan-objs	:= \
		net80211/if_media.o		\
		net80211/rc4.o			\
		net80211/ieee80211.o		\
		net80211/ieee80211_crypto.o	\
		net80211/ieee80211_input.o	\
		net80211/ieee80211_node.o	\
		net80211/ieee80211_output.o	\
		net80211/ieee80211_proto.o	\
		net80211/ieee80211_wireless.o	\
		net80211/ieee80211_linux.o	\
		net80211/ieee80211_crypto_none.o

wlan_wep-objs	:= net80211/ieee80211_crypto_wep.o
wlan_tkip-objs	:= net80211/ieee80211_crypto_tkip.o
wlan_ccmp-objs	:= net80211/ieee80211_crypto_ccmp.o
wlan_xauth-objs	:= net80211/ieee80211_xauth.o
wlan_auth-objs	:= net80211/ieee80211_dot1x.o
wlan_radius-objs:= net80211/ieee80211_radius.o
wlan_acl-objs	:= net80211/ieee80211_acl.o

obj-$(CONFIG_MARVELL_SOFTAP) += \
				mwl.o 		\
				wlan.o		\
				wlan_wep.o	\
				wlan_tkip.o	\
				wlan_ccmp.o	\
				wlan_xauth.o	\
				wlan_acl.o

PWD:=$(shell pwd)/drivers/net/wireless/marvell_softap

# COPTS+= -DMWL_DEBUG -DMWL_DEBUG_VERBOSE -DMWL_PCI
#EXTRA_CFLAGS+=-DMWL_KERNEL_26 -DFEROCEON

EXTRA_CFLAGS+=-DMWL_KERNEL_26
EXTRA_CFLAGS+=-Ih -I${PWD} -I${PWD}/firmware -I${PWD}/net80211 -I${PWD}/driver
EXTRA_CFLAGS+= -include ${PWD}/net80211/compat.h

###############################################################################
#
# End of file
#
###############################################################################
