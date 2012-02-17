###############################################################################
#
# Name:        Makefile
# Project:     Linux SoftAP for Marvell CB35 cardbus cards
# Version:     $Revision: 1.1.1.1 $
# Date:        $Date: 2009/08/19 08:02:21 $
# Author:      Ralph Roesler (rroesler@syskonnect.de)
# Purpose:     builds the object files
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

obj-m	+= mwl.o
mwl-objs := \
		mwl_debug.o		\
		mwl_descriptors.o	\
		mwl_download.o		\
		mwl_ethtool.o		\
		mwl_hostcmd.o		\
		mwl_hotplug.o		\
		mwl_main.o		\
		mwl_wlanext.o		\
		mwl_xmitrecv.o

ifeq ($(CURRDIR),)
CURRDIR=/root/mwl-0.2
endif
FW=firmware
NET80211=net80211

ifndef KERNELPATH
$(error KERNELPATH must be defined: try 'make KERNELPATH=/usr/src/linux'.)
endif

ifndef KERNELRELEASE
VERSION := $(strip $(shell head -n 1 ${KERNELPATH}/Makefile | cut -f 2 -d'='))
PATCHLEVEL := $(strip $(shell head -n 2 ${KERNELPATH}/Makefile | tail -1 | cut -f 2 -d'='))
SUBVERSION := $(strip $(shell head -n 3 ${KERNELPATH}/Makefile | tail -1 | cut -f 2 -d'='))
KERNELRELEASE := $(strip ${VERSION}.${PATCHLEVEL}.${SUBVERSION})
endif

ifeq ($(KERNELRELEASE),)
KERNELRELEASE = $(shell uname -r)
endif

ifneq ($(findstring 2.6,$(KERNELRELEASE)),)
export-objs     :=
list-multi      :=
KMODSUF         := ko
COPTS+= -DMWL_KERNEL_26
else
KMODSUF         := o
COPTS+= -DMWL_KERNEL_24
endif

ifeq ($(KERNELPATH),)
KERNELPATH=	/lib/modules/${KERNELRELEASE}/build
endif

ifeq ($(DESTDIR),)
DESTDIR=
endif

ifeq ($(MODULEPATH),)
MODULEPATH=	/lib/modules/${KERNELRELEASE}/net
endif

###############################################################################
#
# Evaluate dedicated environment settings
#
###############################################################################

ifeq ($(PLATFORM),)
PLATFORM = $(shell uname -m)
endif

ifneq ($(findstring ppc,$(PLATFORM)),)
COPTS+= -DUSE_BIG_ENDIAN
endif

ifneq ($(findstring armv5b,$(PLATFORM)),)
COPTS+= -DUSE_BIG_ENDIAN
endif

ifeq ($(ARCH), arm)
COPTS+= -DUSE_BIG_ENDIAN
endif

###############################################################################
#
# Evaluate and debug compile flags
#
###############################################################################

ifdef MWL_DEBUG
COPTS+= -DMWL_DEBUG
endif

ifdef MWL_DEBUG_VERBOSE
COPTS+= -DMWL_DEBUG_VERBOSE
endif

COPTS+= -DMWL_PCI 
ifdef CONFIG_SMP
COPTS+= -D__SMP__
endif
INCS+=	-Ih -I${CURRDIR} -I${CURRDIR}/${FW} -I${CURRDIR}/${NET80211}

EXTRA_CFLAGS+= ${INCS} 
EXTRA_CFLAGS+= ${COPTS}
EXTRA_CFLAGS+= -include ${CURRDIR}/${NET80211}/compat.h

-include $(TOPDIR)/Rules.make

all:
	$(MAKE) -C $(KERNELPATH) SUBDIRS=$(shell pwd) modules

install: all
	test -d ${DESTDIR}/${MODULEPATH} || mkdir -p ${DESTDIR}/${MODULEPATH}
	cp mwl${BUSNAME}.${KMODSUF} ${DESTDIR}/${MODULEPATH}

clean:
	-rm -f *~ *.o *.ko *.mod.c
	-rm -f .version .*.o.flags .*.o.d .*.o.cmd .*.ko.cmd
	-rm -rf .tmp_versions

mwl.o: ${mwl-objs}
	$(LD) $(LDOPTS) -o mwl.$(KMODSUF) -r $(mwl-objs)

###############################################################################
#
# End of file
#
###############################################################################
