/*
 *  linux/fs/proc/link.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  /proc link-file handling code
 */

#include <asm/segment.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

static int proc_readlink(struct inode *, char *, int);
static int proc_follow_link(struct inode *, struct inode *, int, int, struct inode **);

/*
 * links can't do much...
 */
struct inode_operations proc_link_inode_operations = {
	NULL,			/* no file-operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	proc_readlink,		/* readlink */
	proc_follow_link,	/* follow_link */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static int proc_follow_link(struct inode * dir, struct inode * inode,
	int flag, int mode, struct inode ** res_inode)
{
	unsigned int pid, ino;
	struct task_struct * p;
	int i;

	*res_inode = NULL;
	if (dir)
		iput(dir);
	if (!inode)
		return -ENOENT;
	if (!permission(inode, MAY_EXEC)) {
		iput(inode);
		return -EACCES;
	}
	ino = inode->i_ino;
	pid = ino >> 16;
	ino &= 0x0000ffff;
	iput(inode);
	for (i = 0 ; i < NR_TASKS ; i++)
		if ((p = task[i]) && p->pid == pid)
			break;
	if (i >= NR_TASKS)
		return -ENOENT;
	inode = NULL;
	switch (ino) {
		case PROC_PID_CWD:
			inode = p->fs->pwd;
			break;
		case PROC_PID_ROOT:
			inode = p->fs->root;
			break;
		case PROC_PID_EXE: {
			struct vm_area_struct * vma = p->mm->mmap;
			while (vma) {
				if (vma->vm_flags & VM_EXECUTABLE) {
					inode = vma->vm_inode;
					break;
				}
				vma = vma->vm_next;
			}
			break;
		}
		default:
			switch (ino >> 8) {
				case PROC_PID_FD_DIR:
					ino &= 0xff;
					if (ino < NR_OPEN && p->files->fd[ino])
						inode = p->files->fd[ino]->f_inode;
					break;
			}
	}
	if (!inode)
		return -ENOENT;
	*res_inode = inode;
	inode->i_count++;
	return 0;
}

static int proc_readlink(struct inode * inode, char * buffer, int buflen)
{
	int i;
	unsigned int dev,ino;
	char buf[64];

	if (!S_ISLNK(inode->i_mode)) {
		iput(inode);
		return -EINVAL;
	}
	i = proc_follow_link(NULL, inode, 0, 0, &inode);
	if (i)
		return i;
	if (!inode)
		return -EIO;
	dev = inode->i_dev;
	ino = inode->i_ino;
	iput(inode);
	i = sprintf(buf,"[%04x]:%u", dev, ino);
	if (buflen > i)
		buflen = i;
	i = 0;
	while (i < buflen)
		put_fs_byte(buf[i++],buffer++);
	return i;
}
