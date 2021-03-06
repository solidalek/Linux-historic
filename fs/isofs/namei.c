/*
 *  linux/fs/isofs/namei.c
 *
 *  (C) 1992  Eric Youngdale Modified for ISO9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 */

#include <linux/sched.h>
#include <linux/iso_fs.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <asm/segment.h>
#include <linux/malloc.h>

#include <linux/errno.h>

/*
 * ok, we cannot use strncmp, as the name is not in our data space.
 * Thus we'll have to use isofs_match. No big problem. Match also makes
 * some sanity tests.
 *
 * NOTE! unlike strncmp, isofs_match returns 1 for success, 0 for failure.
 */
static int isofs_match(int len,const char * name, char * compare, int dlen)
{
	register int same;
	
	if (!compare) return 0;
	/* "" means "." ---> so paths like "/usr/lib//libc.a" work */
	if (!len && (compare[0]==0) && (dlen==1))
		return 1;
	
	if (compare[0]==0 && dlen==1 && len == 1)
		compare = ".";
 	if (compare[0]==1 && dlen==1 && len == 2) {
		compare = "..";
		dlen = 2;
	};
#if 0
	if (len <= 2) printk("Match: %d %d %s %d %d \n",len,dlen,compare,de->name[0], dlen);
#endif
	
	if (dlen != len)
		return 0;
	__asm__ __volatile__(
		"cld\n\t"
		"repe ; cmpsb\n\t"
		"setz %%al"
		:"=a" (same)
		:"0" (0),"S" ((long) name),"D" ((long) compare),"c" (len)
		:"cx","di","si");
	return same;
}

/*
 *	isofs_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as an inode number). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 */
static struct buffer_head * isofs_find_entry(struct inode * dir,
	const char * name, int namelen, unsigned long * ino, unsigned long * ino_back)
{
	unsigned long bufsize = ISOFS_BUFFER_SIZE(dir);
	unsigned char bufbits = ISOFS_BUFFER_BITS(dir);
	unsigned int block, i, f_pos, offset, inode_number;
	struct buffer_head * bh;
	void * cpnt = NULL;
	unsigned int old_offset;
	unsigned int backlink;
	int dlen, rrflag, match;
	char * dpnt;
	struct iso_directory_record * de;
	char c;

	*ino = 0;
	if (!dir) return NULL;
	
	if (!(block = dir->u.isofs_i.i_first_extent)) return NULL;
  
	f_pos = 0;

	offset = f_pos & (bufsize - 1);
	block = isofs_bmap(dir,f_pos >> bufbits);

	if (!block || !(bh = bread(dir->i_dev,block,bufsize))) return NULL;
  
	while (f_pos < dir->i_size) {
		de = (struct iso_directory_record *) (bh->b_data + offset);
		backlink = dir->i_ino;
		inode_number = (block << bufbits) + (offset & (bufsize - 1));

		/* If byte is zero, this is the end of file, or time to move to
		   the next sector. Usually 2048 byte boundaries. */
		
		if (*((unsigned char *) de) == 0) {
			brelse(bh);
			offset = 0;
			f_pos = ((f_pos & ~(ISOFS_BLOCK_SIZE - 1))
				 + ISOFS_BLOCK_SIZE);
			block = isofs_bmap(dir,f_pos>>bufbits);
			if (!block || !(bh = bread(dir->i_dev,block,bufsize)))
				return 0;
			continue; /* Will kick out if past end of directory */
		}

		old_offset = offset;
		offset += *((unsigned char *) de);
		f_pos += *((unsigned char *) de);

		/* Handle case where the directory entry spans two blocks.
		   Usually 1024 byte boundaries */
		if (offset >= bufsize) {
		        unsigned int frag1;
			frag1 = bufsize - old_offset;
			cpnt = kmalloc(*((unsigned char *) de),GFP_KERNEL);
			memcpy(cpnt, bh->b_data + old_offset, frag1);

			de = (struct iso_directory_record *) cpnt;
			brelse(bh);
			offset = f_pos & (bufsize - 1);
			block = isofs_bmap(dir,f_pos>>bufbits);
			if (!block || !(bh = bread(dir->i_dev,block,bufsize))) {
			        kfree(cpnt);
				return 0;
			};
			memcpy((char *)cpnt+frag1, bh->b_data, offset);
		}
		
		/* Handle the '.' case */
		
		if (de->name[0]==0 && de->name_len[0]==1) {
			inode_number = dir->i_ino;
			backlink = 0;
		}
		
		/* Handle the '..' case */

		if (de->name[0]==1 && de->name_len[0]==1) {
#if 0
			printk("Doing .. (%d %d)",
			       dir->i_sb->s_firstdatazone << bufbits,
			       dir->i_ino);
#endif
			if((dir->i_sb->u.isofs_sb.s_firstdatazone
			    << bufbits) != dir->i_ino)
				inode_number = dir->u.isofs_i.i_backlink;
			else
				inode_number = dir->i_ino;
			backlink = 0;
		}
    
		dlen = de->name_len[0];
		dpnt = de->name;
		/* Now convert the filename in the buffer to lower case */
		rrflag = get_rock_ridge_filename(de, &dpnt, &dlen, dir);
		if (rrflag) {
		  if (rrflag == -1) goto out; /* Relocated deep directory */
		} else {
		  if(dir->i_sb->u.isofs_sb.s_mapping == 'n') {
		    for (i = 0; i < dlen; i++) {
		      c = dpnt[i];
		      if (c >= 'A' && c <= 'Z') c |= 0x20;  /* lower case */
		      if (c == ';' && i == dlen-2 && dpnt[i+1] == '1') {
			dlen -= 2;
			break;
		      }
		      if (c == ';') c = '.';
		      de->name[i] = c;
		    }
		    /* This allows us to match with and without a trailing
		       period.  */
		    if(dpnt[dlen-1] == '.' && namelen == dlen-1)
		      dlen--;
		  }
		}
		match = isofs_match(namelen,name,dpnt,dlen);
		if (cpnt) {
			kfree(cpnt);
			cpnt = NULL;
		}

		if(rrflag) kfree(dpnt);
		if (match) {
			if(inode_number == -1) {
				/* Should only happen for the '..' entry */
				inode_number = 
					isofs_lookup_grandparent(dir,
					   find_rock_ridge_relocation(de,dir));
				if(inode_number == -1){
					/* Should never happen */
					printk("Backlink not properly set.\n");
					goto out;
				}
			}
			*ino = inode_number;
			*ino_back = backlink;
			return bh;
		}
	}
 out:
	if (cpnt)
		kfree(cpnt);
	brelse(bh);
	return NULL;
}

int isofs_lookup(struct inode * dir,const char * name, int len,
	struct inode ** result)
{
	unsigned long ino, ino_back;
	struct buffer_head * bh;

#ifdef DEBUG
	printk("lookup: %x %d\n",dir->i_ino, len);
#endif
	*result = NULL;
	if (!dir)
		return -ENOENT;

	if (!S_ISDIR(dir->i_mode)) {
		iput(dir);
		return -ENOENT;
	}

	ino = 0;

	if (dcache_lookup(dir, name, len, &ino)) ino_back = dir->i_ino;

	if (!ino) {
	  if (!(bh = isofs_find_entry(dir,name,len, &ino, &ino_back))) {
	    iput(dir);
	    return -ENOENT;
	  }
	  if (ino_back == dir->i_ino)
		dcache_add(dir, name, len, ino);
	  brelse(bh);
	};

	if (!(*result = iget(dir->i_sb,ino))) {
		iput(dir);
		return -EACCES;
	}

	/* We need this backlink for the ".." entry unless the name that we
	   are looking up traversed a mount point (in which case the inode
	   may not even be on an iso9660 filesystem, and writing to
	   u.isofs_i would only cause memory corruption).
	*/
	
	if (ino_back && !(*result)->i_pipe && (*result)->i_sb == dir->i_sb) {
	  (*result)->u.isofs_i.i_backlink = ino_back; 
	}
	
	iput(dir);
	return 0;
}
