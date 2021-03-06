#ifndef _VT_KERN_H
#define _VT_KERN_H

/*
 * this really is an extension of the vc_cons structure in console.c, but
 * with information needed by the vt package
 */

#include <linux/vt.h>

extern struct vt_struct {
	int vc_num;				/* The console number */
	unsigned char	vc_mode;		/* KD_TEXT, ... */
	unsigned char	vc_kbdraw;
	unsigned char	vc_kbde0;
	unsigned char   vc_kbdleds;
	struct vt_mode	vt_mode;
	int		vt_pid;
	int		vt_newvt;
	struct wait_queue *paste_wait;
} vt_cons[NR_CONSOLES];

void kd_mksound(unsigned int count, unsigned int ticks);

#endif /* _VT_KERN_H */
