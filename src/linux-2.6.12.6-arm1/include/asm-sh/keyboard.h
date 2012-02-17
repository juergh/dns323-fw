#ifndef	__ASM_SH_KEYBOARD_H
#define	__ASM_SH_KEYBOARD_H
/*
 *	$Id: keyboard.h,v 1.1.1.1 2009/08/19 08:02:43 jack Exp $
 */

#include <linux/kd.h>
#include <linux/config.h>
#include <asm/machvec.h>

#ifdef CONFIG_SH_MPC1211
#include <asm/mpc1211/keyboard-mpc1211.h>
#endif
#endif
