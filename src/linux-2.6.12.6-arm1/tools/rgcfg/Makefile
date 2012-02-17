#
# Makefile for the RGCFG application
#

TGT = rgcfg
APP = $(TGT)
OBJS = rgcfg.o
 
DEBUG = -DNDEBUG
OPT   = -O2

CFLAGS = -I. -I../../arch/arm/mach-mv88fxx81/Board/QDDriver/qddrv/Include \
		-I../../arch/arm/mach-mv88fxx81/Board/QDDriver/include	\
		-I../../arch/arm/mach-mv88fxx81/Board/QDDriver/qddrv/Include/h/msApi \
		-I../../arch/arm/mach-mv88fxx81/Board/QDDriver/qddrv/Include/h/driver \
		-I../../arch/arm/mach-mv88fxx81/Board/QDDriver/qddrv/Include/h/platform\
		-I../../arch/arm/mach-mv88fxx81/Common

$(TGT): $(OBJS)
	$(CC) $^ -o $@

clean:
	rm *.o $(TGT)
