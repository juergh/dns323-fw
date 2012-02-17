#
# Copyright (c) 2002-2004 Sam Leffler, Errno Consulting
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer,
#    without modification.
# 2. Redistributions in binary form must reproduce at minimum a disclaimer
#    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
#    redistribution must be conditioned upon including a substantially
#    similar Disclaimer requirement for further binary redistribution.
# 3. Neither the names of the above-listed copyright holders nor the names
#    of any contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# Alternatively, this software may be distributed under the terms of the
# GNU General Public License ("GPL") version 2 as published by the Free
# Software Foundation.
#
# NO WARRANTY
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
# AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
# THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
# OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
# IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGES.
#
# $Id: Makefile,v 1.1.1.1 2009/08/19 08:02:21 jack Exp $
#

ifeq ($(CURRDIR),)
CURRDIR=/root/mwl-0.2
endif
NET80211=net80211

#
# Makefile for the 802.11 WLAN modules.
#
ifeq ($(obj),)
obj=	.
endif

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
else
KMODSUF         := o
endif

ifeq ($(KERNELPATH),)
KERNELPATH=     /lib/modules/${KERNELRELEASE}/build
endif

ifeq ($(DESTDIR),)
DESTDIR=
endif

ifeq ($(MODULEPATH),)
MODULEPATH=     /lib/modules/${KERNELRELEASE}/net
endif

DEPTH=	..

#
# There are two authenticator mechanisms: an in-kernel implementation
# (wlan_auth+wlan_radius) and an external implementation (wlan_xauth) that
# requires a user process to manage the authentication process.  By default
# the external authenticator is used.  ieee80211_proto.c has a table of module
# names that defines the default module to auto-load for each authentication
# scheme; to get the in-kernel authenticator by default modify it to load
# wlan_auth instead of wlan_xauth or manually load wlan_auth prior to use.
# 
MOD_AUTH	:= wlan_xauth.o
#
# Crypto test modules.  These are used only to test/validate the
# software crypto implementations.
#
MOD_CRYPTO_TEST	:= tkip_test.o ccmp_test.o wep_test.o
#
# Modules to be built+installed (leave out the crypto test modules).
#
MOD_INSTALL	:= wlan.o wlan_wep.o wlan_tkip.o wlan_ccmp.o wlan_acl.o \
		   $(MOD_AUTH)

obj-m		+= $(MOD_INSTALL)

wlan-objs	:= if_media.o rc4.o \
		   ieee80211.o ieee80211_crypto.o ieee80211_input.o \
		   ieee80211_node.o ieee80211_output.o ieee80211_proto.o \
		   ieee80211_wireless.o ieee80211_linux.o \
		   ieee80211_crypto_none.o
wlan_wep-objs	:= ieee80211_crypto_wep.o
wlan_tkip-objs	:= ieee80211_crypto_tkip.o
wlan_ccmp-objs	:= ieee80211_crypto_ccmp.o
wlan_acl-objs	:= ieee80211_acl.o
wlan_auth-objs	:= ieee80211_dot1x.o
wlan_radius-objs:= ieee80211_radius.o
wlan_xauth-objs	:= ieee80211_xauth.o

tkip_test-objs	:= test_tkip.o
ccmp_test-objs	:= test_ccmp.o
wep_test-objs	:= test_wep.o

ifeq ($(findstring 2.6,$(KERNELRELEASE)),)
export-objs	:= if_media.o rc4.o \
		   ieee80211.o ieee80211_crypto.o ieee80211_input.o \
		   ieee80211_node.o ieee80211_output.o ieee80211_proto.o \
		   ieee80211_wireless.o ieee80211_linux.o \
		   ieee80211_crypto_none.o ieee80211_crypto_wep.o \
		   ieee80211_crypto_tkip.o ieee80211_crypto_ccmp.o \
		   ieee80211_dot1x.o ieee80211_radius.o ieee80211_xauth.o
list-multi	:= $(MOD_INSTALL)
endif

INCS+=	-Ih -I${CURRDIR} -I${CURRDIR}/${NET80211}

EXTRA_CFLAGS+=$(INCS) ${COPTS} -DCONFIG_NET_WIRELESS 
EXTRA_CFLAGS+= -include ${CURRDIR}/${NET80211}/compat.h

ifdef CONFIG_SMP
EXTRA_CFLAGS+= -D__SMP__
endif

-include $(TOPDIR)/Rules.make

all:
	$(MAKE) -C $(KERNELPATH) SUBDIRS=$(shell pwd) modules

wlan.o:	$(wlan-objs)
	$(LD) $(LDOPTS) -o wlan.$(KMODSUF) -r $(wlan-objs)
wlan_wep.o:	$(wlan_wep-objs)
	$(LD) $(LDOPTS) -o wlan_wep.$(KMODSUF) -r $(wlan_wep-objs)
wlan_tkip.o:	$(wlan_tkip-objs)
	$(LD) $(LDOPTS) -o wlan_tkip.$(KMODSUF) -r $(wlan_tkip-objs)
wlan_ccmp.o:	$(wlan_ccmp-objs)
	$(LD) $(LDOPTS) -o wlan_ccmp.$(KMODSUF) -r $(wlan_ccmp-objs)
wlan_xauth.o:	$(wlan_xauth-objs)
	$(LD) $(LDOPTS) -o wlan_xauth.$(KMODSUF) -r $(wlan_xauth-objs)
wlan_auth.o:	$(wlan_auth-objs)
	$(LD) $(LDOPTS) -o wlan_auth.$(KMODSUF) -r $(wlan_auth-objs)
wlan_radius.o:	$(wlan_radius-objs)
	$(LD) $(LDOPTS) -o wlan_radius.$(KMODSUF) -r $(wlan_radius-objs)
wlan_acl.o:	$(wlan_acl-objs)
	$(LD) $(LDOPTS) -o wlan_acl.$(KMODSUF) -r $(wlan_acl-objs)

tkip_test.o:	$(tkip_test-objs)
	$(LD) $(LDOPTS) -o tkip_test.$(KMODSUF) -r $(tkip_test-objs)
ccmp_test.o:	$(ccmp_test-objs)
	$(LD) $(LDOPTS) -o ccmp_test.$(KMODSUF) -r $(ccmp_test-objs)
wep_test.o:	$(wep_test-objs)
	$(LD) $(LDOPTS) -o wep_test.$(KMODSUF) -r $(wep_test-objs)

install: all
	test -d ${DESTDIR}/${MODULEPATH} || mkdir -p ${DESTDIR}/${MODULEPATH}
	for i in ${MOD_INSTALL}; do \
		f=`basename $$i .o`; \
		cp $$f.${KMODSUF} ${DESTDIR}/${MODULEPATH}; \
	done

clean:
	-rm -f *~ *.o *.ko *.mod.c
	-rm -f .version .*.o.flags .*.o.d .*.o.cmd .*.ko.cmd
	-rm -rf .tmp_versions
