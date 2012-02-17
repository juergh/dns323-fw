# $Id: Makefile,v 1.1.1.1 2009/08/19 08:01:46 jack Exp $
#
# Makefile for the linux kernel.
#

extra-y	:= head.o


obj-y   := entry.o traps.o shadows.o debugport.o irq.o \
	   process.o setup.o signal.o traps.o time.o ptrace.o

obj-$(CONFIG_ETRAX_KGDB) += kgdb.o
obj-$(CONFIG_ETRAX_FAST_TIMER) += fasttimer.o
obj-$(CONFIG_MODULES)    += crisksyms.o

clean:

