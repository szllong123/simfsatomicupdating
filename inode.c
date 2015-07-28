/*
 * linux/fs/nvmm/inode.c
 *
 * Copyright (C) 2013 College of Computer Science,
 * Chonqing University
 *
 * inode operations
 *
 */

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/hardirq.h>
#include <linux/sched.h>
#include <linux/highuid.h>
#include <linux/quotaops.h>
#include <linux/sched.h>
#include <linux/mpage.h>
#include <linux/backing-dev.h>
#include "nvmm.h"
#include "xattr.h"
#include "xip.h"
#include "acl.h"


//static DEFINE_SPINLOCK(read_inode_lock);
//static DEFINE_SPINLOCK(update_inode_lock);

void nvmm_set_inode_flags(struct inode *inode);
static int __nvmm_write_inode(struct inode *inode, int do_sync);

struct backing_dev_info nvmm_backing_dev_info __read_mostly = {
	.ra_pages       = 0,    /* No readahead */
	.capabilities	= BDI_CAP_NO_ACCT_AND_WRITEBACK,
};

/*
 * input:
 * @inode : vfs inode
 * @zero : if initialize the page
 *
 * output:
 * @blocknr : page frame num
 * returns if allock succuss
 *
 * allocate a data block for inode and return it's absolute phy address frame.
 * Zeroes out the block if zero set. Increments inode->i_blocks. 
 * if get a new data block,return 0 and the second parameter stores its phy page number
 * if the third parameter is set the new data block should be initialized
 */
/*static int nvmm_new_data_block(struct inode *inode, unsigned long *blocknr, int zero)
{
	int err = nvmm_new_block(inode->i_sb, blocknr, zero);
	if(!err)
	{
		struct nvmm_inode *ni = nvmm_get_inode(inode->i_sb, inode->i_ino);
		inode->i_blocks++;
		nvmm_memunlock_inode(inode->i_sb, ni);
		ni->i_blocks =  cpu_to_le32(inode->i_blocks);
		nvmm_memlock_inode(inode->i_sb, ni);
	}

	return err;
}*///end function nvmm_new_data_block

/*
 *
 * input:
 * @inode : vfs inode
 * @file_blocknr : the block number of this inode
 * 
 * output:
 *
 * return :
 * the physaddr of the block
 *
 * find the offset to the block represented by the given inode's file
 * relative block number.
 */
u64 nvmm_find_data_block(struct inode *inode, unsigned long file_blocknr)
{
	struct super_block *sb = inode->i_sb;
	struct nvmm_inode *ni = NULL;

	u64 *first_lev = NULL;		/* ptr to first level */
	u64 *second_lev = NULL;	/* ptr to second level */
	u64 *third_lev = NULL;		/* ptr to the third level*/
	u64 first_phys = 0;
	u64 second_phys = 0;
	u64 third_phys = 0;
	u64 bp = 0;		/* phys of this number*/
	int entry_offset = 511;
	int entry_num = 9;	
	int first_num = 0, second_num = 0, third_num = 0;

	first_num = ((file_blocknr >> entry_num ) >> entry_num) & entry_offset;
	second_num = (file_blocknr >> entry_num) & entry_offset;
	third_num = file_blocknr & entry_offset;

	ni = nvmm_get_inode(sb, inode->i_ino);

	first_phys = le64_to_cpu(ni->i_pg_addr);
	first_lev = __va(first_phys);
//	printk("the first_lev is :%lu\n", *first_lev);
	if (first_lev) {
		second_phys = le64_to_cpu(first_lev[first_num]) & PAGE_MASK;
		second_lev = __va(second_phys);
		if (second_lev){
			third_phys = le64_to_cpu(second_lev[second_num]) & PAGE_MASK;
			third_lev = __va(third_phys);
			if(third_lev)
				bp = (le64_to_cpu(third_lev[third_num]) & 
						0x0fffffffffffffff) & PAGE_MASK;
		}
	}

	return bp;
}


/*
 * input:
 * @inode : vfs inode 
 * @start 
 * @end
 * free data block from range start<=>end
 */
static void __nvmm_truncate_blocks(struct inode *inode, loff_t start, loff_t end)
{
	
	struct super_block *sb = inode->i_sb;
	struct nvmm_inode *ni;
	unsigned long first_blocknr,last_blocknr;
	struct nvmm_inode_info *ni_info;
	pud_t *pud;
	unsigned long ino;
	unsigned long vaddr;
	struct mm_struct *mm;
	mm = current->mm;
	ni_info = NVMM_I(inode);
	vaddr = (unsigned long)ni_info->i_virt_addr;
	ino = inode->i_ino;
	pud = nvmm_get_pud(sb, ino);
	ni = nvmm_get_inode(sb, ino);

	if(!ni->i_pg_addr)
		return ;
	
	first_blocknr = (start + sb->s_blocksize-1) >> sb->s_blocksize_bits;
	
	if(ni->i_flags & cpu_to_le32(NVMM_EOFBLOCKS_FL))
		last_blocknr = (1UL << (2*sb->s_blocksize_bits - 6))-1;
	else
		last_blocknr = end >> sb->s_blocksize_bits;

	if(first_blocknr > last_blocknr)
		return;

	unnvmap(vaddr, pud, mm);
	nvmm_rm_pg_table(sb, inode->i_ino);
	inode->i_blocks = 0;
	
}//end function __nvmm_truncate_blocks


/*
 * input :
 * @inode : vfs inode
 * @start 
 * @end
 * free data blocks from inode ,this is used in nvmm_evict_inode()
 *pram and ext2 realize it in different way
 */
static void nvmm_truncate_blocks(struct inode *inode, loff_t start, loff_t end)
{
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	      S_ISLNK(inode->i_mode)))
		return;

	__nvmm_truncate_blocks(inode, start, end);
	inode->i_mtime = inode->i_ctime = CURRENT_TIME_SEC;
	nvmm_update_inode(inode);
}//end function nvmm_truncate_blocks


/*
 * input :
 * @inode : vfs inode
 * @num :  num of data block to be requested
 *
 * returns :
 * alloc and insert blocks to inode.
 * success return 0,else return others.
 */
int nvmm_alloc_blocks(struct inode *inode, int num)
{
	struct super_block *sb = inode->i_sb;
	int errval = 0,i = 0;
	//unsigned long blocknr;
	int ino = inode->i_ino;
	pud_t *pud;
	unsigned long vaddr, *p;
	struct mm_struct *mm;
	struct nvmm_inode_info *ni_info;
	struct nvmm_inode *ni = nvmm_get_inode(sb, ino);
    struct nvmm_sb_info *nsi = NVMM_SB(sb);
    struct page *pg;
    phys_addr_t phys, base, next, cur;
    mm = current->mm;
    base = nsi->phy_addr;
	ni_info = NVMM_I(inode);
	vaddr = (unsigned long)ni_info->i_virt_addr;
	if(!ni->i_pg_addr){
		nvmm_init_pg_table(sb, ino);
		pud = nvmm_get_pud(sb, ino);
		nvmap(vaddr, pud, mm);
	}
	if(0 == num){
		return errval;
	}
//	printk("before alloc\n");
	errval = nvmm_new_block(sb, &phys, 1, num);
//	printk("after alloc\n");
    if(!errval){
        cur = phys;

        for(i = 0;i < num; i++)
        {
//            printk(KERN_INFO "MY_DEBUG:pfn=%lx\n", cur);
            p = (unsigned long *)__va(cur);
            next = base + *p;
//			memset((void *)p, 0, PAGE_SIZE);
            pg = pfn_to_page(cur >> PAGE_SHIFT);
            inode->i_blocks++;
            ni->i_blocks =  cpu_to_le32(inode->i_blocks);
            errval = nvmm_insert_page(sb, inode, pg);

            if(unlikely(errval != 0))
                return errval;
            cur = next;
        }
        return errval;
    }else{
        nvmm_error(sb, __FUNCTION__, "no block space left!\n");
        return -ENOSPC;
    }
}//end function nvmm_alloc_blocks


/*
 * input :
 * @inode : vfs inode
 * @ni : nvmm_inode
 * returns :
 * return 0 if read successfully else return others
 * pass nvmm_inode information to vfs inode
 */
static int nvmm_read_inode(struct inode *inode, struct nvmm_inode *ni)
{
	int ret = -EIO;
	struct nvmm_inode_info *ni_info;
	ni_info = NVMM_I(inode);
//	mutex_lock(&NVMM_I(inode)->i_meta_mutex);
//	spin_lock(&read_inode_lock);
	spin_lock(&ni_info->i_meta_spinlock);
	 if (nvmm_calc_checksum((u8 *)ni, NVMM_INODE_SIZE)) { 
	 	nvmm_error(inode->i_sb, (char *)nvmm_read_inode, (char *)"checksum error in inode %08x\n",  (u32)inode->i_ino); 
	 	goto bad_inode;
	 }

	inode->i_mode = le32_to_cpu(ni->i_mode);
	i_uid_write(inode, le32_to_cpu(ni->i_uid));
	i_gid_write(inode, le32_to_cpu(ni->i_gid));
	set_nlink(inode, le32_to_cpu(ni->i_link_counts));
	inode->i_size = le64_to_cpu(ni->i_size);
	inode->i_atime.tv_sec = le32_to_cpu(ni->i_atime);
	inode->i_ctime.tv_sec = le32_to_cpu(ni->i_ctime);
	inode->i_mtime.tv_sec = le32_to_cpu(ni->i_mtime);
	inode->i_atime.tv_nsec = inode->i_mtime.tv_nsec =
		inode->i_ctime.tv_nsec = 0;
	
	inode->i_generation = le32_to_cpu(ni->i_generation);
	nvmm_set_inode_flags(inode);
	/* check if the inode is active. */
	if (inode->i_nlink == 0 && (inode->i_mode == 0 || le32_to_cpu(ni->i_dtime))) {
		/* this inode is deleted */
		nvmm_dbg("read inode: inode %lu not active", inode->i_ino);
		ret = -EINVAL;
		goto bad_inode;
	}

	inode->i_blocks = le64_to_cpu(ni->i_blocks);
	ni_info->i_flags = le32_to_cpu(ni->i_flags);
	ni_info->i_file_acl = le32_to_cpu(ni->i_file_acl);
	ni_info->i_dir_acl = 0;
	ni_info->i_dtime = 0;
	ni_info->i_state = 0;
	ni_info->i_flags = le32_to_cpu(ni->i_flags);
	//inode->i_ino = nvmm_get_inodenr(inode->i_sb, (unsigned long)ni);
	inode->i_mapping->a_ops = &nvmm_aops;
	inode->i_mapping->backing_dev_info = &nvmm_backing_dev_info;

	insert_inode_hash(inode);
	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		if (nvmm_use_xip(inode->i_sb)) {
			inode->i_mapping->a_ops = &nvmm_aops_xip;
			inode->i_fop = &nvmm_xip_file_operations;
		} else {
			inode->i_op = &nvmm_file_inode_operations;
			inode->i_fop = &nvmm_file_operations;
		}
		break;
	case S_IFDIR:
		inode->i_op = &nvmm_dir_inode_operations;
		inode->i_fop = &nvmm_dir_operations;
		break;
	case S_IFLNK:
		inode->i_op = &nvmm_symlink_inode_operations;
		break;
	default:
		inode->i_size = 0;
		break;
	}

//	mutex_unlock(&NVMM_I(inode)->i_meta_mutex);
//	spin_unlock(&read_inode_lock);

	spin_unlock(&ni_info->i_meta_spinlock);
	return 0;

 bad_inode:
	make_bad_inode(inode);
//	mutex_unlock(&NVMM_I(inode)->i_meta_mutex);
	
	spin_unlock(&ni_info->i_meta_spinlock);
	return ret;
}//end function nvmm_read_inode


/*
 * input :
 * @inode : vfs inode
 *
 * returns :
 * return 0 if update successfully
 * else return others
 */
int nvmm_update_inode(struct inode *inode)
{
	struct nvmm_inode *ni;
	int retval = 0;

	ni = nvmm_get_inode(inode->i_sb, inode->i_ino);
	if (!ni)
		return -EACCES;

//	mutex_lock(&NVMM_I(inode)->i_meta_mutex);
//	spin_lock(&update_inode_lock);

	spin_lock(&NVMM_I(inode)->i_meta_spinlock);


	nvmm_memunlock_inode(inode->i_sb, ni);
	ni->i_mode = cpu_to_le32(inode->i_mode);
	ni->i_uid = cpu_to_le32(inode->i_uid);
	ni->i_gid = cpu_to_le32(inode->i_gid);
	ni->i_link_counts = cpu_to_le32(inode->i_nlink);
	ni->i_size = cpu_to_le64(inode->i_size);
	ni->i_blocks = cpu_to_le64(inode->i_blocks);
	ni->i_atime = cpu_to_le32(inode->i_atime.tv_sec);
	ni->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
	ni->i_mtime = cpu_to_le32(inode->i_mtime.tv_sec);
	ni->i_generation = cpu_to_le32(inode->i_generation);

		
	nvmm_memlock_inode(inode->i_sb, ni);
	spin_unlock(&NVMM_I(inode)->i_meta_spinlock);
//	spin_unlock(&update_inode_lock);
	//mutex_unlock(&NVMM_I(inode)->i_meta_mutex);
	return retval;
}//end function nvmm_update_inode

/*
 * input :
 * @inode : vfs inode
 */
void nvmm_free_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct nvmm_super_block *ns;
	struct nvmm_inode *ni;
	unsigned long offset, inode_phy;

	nvmm_xattr_delete_inode(inode);


//	spin_lock(&superblock_lock);
//	mutex_lock(&NVMM_SB(sb)->s_lock);

	spin_lock(&NVMM_SB(sb)->inode_spinlock);
	/*get phy addr of inode*/
	inode_phy = nvmm_get_inode_phy_addr(sb, inode->i_ino);
	offset = nvmm_phys_to_offset(sb, inode_phy);
	if(inode_phy == -BADINO)
		nvmm_error(sb, __FUNCTION__, (char *)-BADINO);
	ni = nvmm_get_inode(sb, inode->i_ino);
	nvmm_memunlock_inode(sb, ni);

	ni->i_dtime = cpu_to_le32(get_seconds());
	ni->i_pg_addr = 0;

	nvmm_memlock_inode(sb, ni);

	/* increment s_free_inodes_count */
	ns = nvmm_get_super(sb);
	nvmm_memunlock_super(sb, ns);

	ni->i_pg_addr = ns->s_free_inode_start;
	ns->s_free_inode_start = offset;

	le64_add_cpu(&ns->s_free_inode_count, 1);
	if (le64_to_cpu(ns->s_free_inode_count) == le64_to_cpu(ns->s_inode_count) - 1) {
		/* filesystem is empty */
		nvmm_dbg("fs is empty!\n");
	}
	nvmm_memlock_super(sb, ns);
	spin_unlock(&NVMM_SB(sb)->inode_spinlock);
//	spin_unlock(&superblock_lock);
//	mutex_unlock(&NVMM_SB(sb)->s_lock);
}//end function nvmm_free_inode


/*
 * input :
 * @ni_info : nvmm_inode_info
 */
void nvmm_set_inode_flags(struct inode *inode)
{
	unsigned int flags = NVMM_I(inode)->i_flags;

	inode->i_flags &= ~(S_SYNC|S_APPEND|S_IMMUTABLE|S_NOATIME|S_DIRSYNC);
	if (flags & NVMM_SYNC_FL)
		inode->i_flags |= S_SYNC;
	if (flags & NVMM_APPEND_FL)
		inode->i_flags |= S_APPEND;
	if (flags & NVMM_IMMUTABLE_FL)
		inode->i_flags |= S_IMMUTABLE;
	if (flags & NVMM_NOATIME_FL)
		inode->i_flags |= S_NOATIME;
	if (flags & NVMM_DIRSYNC_FL)
		inode->i_flags |= S_DIRSYNC;
}

void nvmm_get_inode_flags(struct nvmm_inode_info *ei)
{
	unsigned int flags = ei->vfs_inode.i_flags;

	ei->i_flags &= ~(NVMM_SYNC_FL|NVMM_APPEND_FL|
			NVMM_IMMUTABLE_FL|NVMM_NOATIME_FL|NVMM_DIRSYNC_FL);
	if (flags & S_SYNC)
		ei->i_flags |= NVMM_SYNC_FL;
	if (flags & S_APPEND)
		ei->i_flags |= NVMM_APPEND_FL;
	if (flags & S_IMMUTABLE)
		ei->i_flags |= NVMM_IMMUTABLE_FL;
	if (flags & S_NOATIME)
		ei->i_flags |= NVMM_NOATIME_FL;
	if (flags & S_DIRSYNC)
		ei->i_flags |= NVMM_DIRSYNC_FL;
}


/*
 * input :
 * @sb : vfs super_block
 * @ino : vfs inode num
 * return :
 * inode : vfs inode
 * get the vfs inode with respect to inode number
 *
 */
struct inode *nvmm_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *inode;
	struct nvmm_inode *ni;
	int err;

	inode = iget_locked(sb, ino);
	inode->i_ino = ino;
	if (unlikely(!inode))
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	ni = nvmm_get_inode(sb, ino);
	if (!ni) {
		err = -EACCES;
		goto fail;
	}
	err = nvmm_read_inode(inode, ni);
	if (unlikely(err))
		goto fail;

	unlock_new_inode(inode);
	return inode;
fail:
	iget_failed(inode);
	return ERR_PTR(err);
}//end function nvmm_iget


/*
 * input:
 * @inode : vfs inode
 *
 * if i_nlink zero,then free the inode 
 */
void nvmm_evict_inode(struct inode * inode)
{
	int want_delete = 0;

	nvmm_establish_mapping(inode);
	if (!inode->i_nlink && !is_bad_inode(inode)){
		want_delete = 1;
		dquot_initialize(inode);
	}else{
		dquot_drop(inode);
	}
	truncate_inode_pages(&inode->i_data, 0);
	if (want_delete) {
		sb_start_intwrite(inode->i_sb);
		/* set dtime */
		NVMM_I(inode)->i_dtime	= get_seconds();
		mark_inode_dirty(inode);
		__nvmm_write_inode(inode, inode_needs_sync(inode));
		nvmm_truncate_blocks(inode, 0,inode->i_size);
		inode->i_size = 0;
	}

	invalidate_inode_buffers(inode);
	clear_inode(inode);

	if (want_delete) {
		nvmm_free_inode(inode);
		sb_end_intwrite(inode->i_sb);
	}
	nvmm_destroy_mapping(inode);
}


struct inode *nvmm_new_inode(struct inode *dir, umode_t mode, const struct qstr *qstr)
{
	struct super_block *sb;
	struct nvmm_sb_info *sbi;
	struct nvmm_super_block *ns;
	struct inode *inode;
	struct nvmm_inode *ni = NULL;
	struct nvmm_inode_info *ni_info;
	struct nvmm_inode *diri = NULL;
	int errval;
	ino_t ino = 0;
	unsigned long offset, ino_phy;

	sb = dir->i_sb;
	inode = new_inode(sb);
	if(!inode)
		return ERR_PTR(-ENOMEM);

	ni_info = NVMM_I(inode);
	sbi = NVMM_SB(sb) ;

//	spin_lock(&superblock_lock);
//	mutex_lock(&NVMM_SB(sb)->s_lock);


	spin_lock(&NVMM_SB(sb)->inode_spinlock);
	ns = nvmm_get_super(sb);

	if (ns->s_free_inode_count) {
		/* find the oldest unused nvmm inode */
		offset = ns->s_free_inode_start;
		ino_phy = nvmm_offset_to_phys(sb, offset);
		ino = nvmm_get_inodenr(sb, ino_phy);
		ni = nvmm_get_inode(sb, ino),
		ns->s_free_inode_start = ni->i_pg_addr;
		ni->i_pg_addr = 0;
		nvmm_dbg("allocating inode %lu\n", ino);
		nvmm_memunlock_super(sb, ns);
		le64_add_cpu(&ns->s_free_inode_count, -1);
		nvmm_memlock_super(sb, ns);
	} else {
		nvmm_dbg("no space left to create new inode!\n");
		errval = -ENOSPC;
		goto fail1;
	}

	diri = nvmm_get_inode(sb, dir->i_ino);
	if(!diri){
		errval = -EACCES;
		goto fail1;
	}

	/* chosen inode is in ino */

	inode->i_ino = ino;
	inode_init_owner(inode, dir, mode);
	inode->i_blocks = inode->i_size = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;

	ni_info->i_pg_addr = 0;
	ni_info->i_flags = nvmm_mask_flags(mode, NVMM_I(dir)->i_flags&NVMM_FL_INHERITED);
	ni_info->i_file_acl = 0;
	ni_info->i_dir_acl = 0;
	ni_info->i_state = NVMM_STATE_NEW;
	ni_info->i_virt_addr = 0;
	atomic_set(&ni_info->i_p_counter, 0);
//	mutex_init(&ni_info->truncate_mutex);
//	mutex_init(&ni_info->i_meta_mutex);
	inode->i_generation = atomic_add_return(1, &sbi->next_generation);

	
	nvmm_set_inode_flags(inode);
	if (insert_inode_locked(inode) < 0) {
		errval = -EINVAL;
		goto fail1;
	}

	errval = nvmm_write_inode(inode, 0);
	if(errval)
		goto fail2;

	errval = nvmm_init_acl(inode, dir);
	if (errval)
		goto fail2;

	errval = nvmm_init_security(inode, dir, qstr);
	if (errval)
		goto fail2;


	spin_unlock(&NVMM_SB(sb)->inode_spinlock);
//	spin_unlock(&superblock_lock);
//	mutex_unlock(&NVMM_SB(sb)->s_lock);
	/*establish pagetable*/
	errval = nvmm_init_pg_table(inode->i_sb, inode->i_ino);

	return inode;
fail2:

	spin_unlock(&NVMM_SB(sb)->inode_spinlock);
//	spin_unlock(&superblock_lock);
//	mutex_unlock(&NVMM_SB(sb)->s_lock);
	clear_nlink(inode);
	unlock_new_inode(inode);
	iput(inode);
	return ERR_PTR(errval);
fail1:
	spin_unlock(&NVMM_SB(sb)->inode_spinlock);
//	spin_unlock(&superblock_lock);
//	mutex_unlock(&NVMM_SB(sb)->s_lock);
	make_bad_inode(inode);
	iput(inode);
	return ERR_PTR(errval);
}//end function nvmm_new_inode

/*
 * input :
 * @inode : vfs inode
 * @wbc : 
 * return :
 * this function just quote nvmm_update_inode()
 */
int nvmm_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	return nvmm_update_inode(inode);
}

/*
 * input :
 * @inode : vfs inode
 * this function is called from _mark_inode_dirty() and quote nvmm_update_inode()
 */
void nvmm_dirty_inode(struct inode *inode)
{
	nvmm_update_inode(inode);
}
/*
 * input :
 * @file : vfs file
 * 
 * output :
 * @page : struct page
 *
 * returns :
 * 0 if success else others
 *
 * this function is used for page cache
 */
static int nvmm_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct super_block *sb = inode->i_sb;
	loff_t offset = 0, size = 0;
	unsigned long fillsize = 0, blocknr = 0, bytes_filled = 0;
	u64 block = 0;
	void *buf = NULL, *bp = NULL;
	int ret = 0;
	//char test[4096];

//	printk("Here is in readpage()\n");
	buf = kmap(page);
	if (!buf)
		return -ENOMEM;

	offset = page_offset(page);
	size = i_size_read(inode);
	blocknr = page->index << (PAGE_CACHE_SHIFT - inode->i_blkbits);
	
	if (offset < size) {
		size -= offset;
		fillsize = size > PAGE_SIZE ? PAGE_SIZE : size;
		while (fillsize) {
			int count = fillsize > sb->s_blocksize ?
						sb->s_blocksize : fillsize;
			block = nvmm_find_data_block(inode, blocknr);
			if (likely(block)) {
				bp = __va(block);
				if (!bp) {
					SetPageError(page);
					bytes_filled = 0;
					ret = -EIO;
					goto out;
				}
				//memcpy(test, bp, count);
				memcpy(buf + bytes_filled, bp, count);
			} else {
				memset(buf + bytes_filled, 0, count);
			}
			bytes_filled += count;
			fillsize -= count;
			blocknr++;
		}
	}
 out:
	if (bytes_filled < PAGE_SIZE)
		memset(buf + bytes_filled, 0, PAGE_SIZE - bytes_filled);
	if (ret == 0)
		SetPageUptodate(page);

	flush_dcache_page(page);
	kunmap(page);
	unlock_page(page);

	return ret;
}


/*
 * called to zeros out a single block.it's used in the "resize"
 * to avoid to keep data in case the file grow up again.
 */
static int nvmm_block_truncate_page(struct inode *inode, loff_t newsize)
{
	struct super_block *sb = inode->i_sb;
	unsigned long offset = newsize & (sb->s_blocksize - 1);
	unsigned long length;
	char *bp;
	int ret = 0;

	if(!offset || newsize > inode->i_size)
		goto out;

	length = sb->s_blocksize - offset;

	bp = NVMM_I(inode)->i_virt_addr;
	if(!bp){
		ret = -EACCES;
		goto out;
	}

	memset(bp + offset, 0, length);

out:
	return ret;
}


/*
 * is called by nvmm_notify_change
 * input :
 * @inode : vfs inode
 * @newsize :type of loff_t(just a long long types)
 *
 * returns :
 * 0 if success
 */
static int nvmm_setsize(struct inode *inode, loff_t newsize)
{
	int ret = 0;
	loff_t oldsize = inode->i_size;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	    S_ISLNK(inode->i_mode)))
		return -EINVAL;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return -EPERM;

	if(newsize != oldsize){
		if (mapping_is_xip(inode->i_mapping))
			ret = xip_truncate_page(inode->i_mapping, newsize);
		else
			ret = nvmm_block_truncate_page(inode, newsize);
		if (ret)
			return ret;

		i_size_write(inode, newsize);
	}

	synchronize_rcu();
	truncate_pagecache(inode, oldsize, newsize);
	__nvmm_truncate_blocks(inode, newsize, oldsize);

	/*check for the flag EOFBOCLKS is still valid after the set size*/
	check_eof_blocks(inode, newsize);
	inode->i_mtime = inode->i_ctime = CURRENT_TIME_SEC;
	nvmm_update_inode(inode);

	return ret;
}

/*
 * input :
 * @inode : vfs inode
 * @do_sync : type of int
 *
 * returns :
 * 0 if succsee else others
 */

static int __nvmm_write_inode(struct inode *inode, int do_sync)
{
	struct nvmm_inode_info *ni_info = NVMM_I(inode);
	struct super_block *sb = inode->i_sb;
	ino_t ino = inode->i_ino;
	struct nvmm_inode * ni = nvmm_get_inode(sb, ino);
	int err = 0;

	if (IS_ERR(ni))
 		return -EIO;

	/* For fields not not tracking in the in-memory inode,
	 * initialise them to zero for new inodes. 
	 */
	if (ni_info->i_state & NVMM_STATE_NEW)
		memset(ni, 0, NVMM_SB(sb)->s_inode_size);
	nvmm_get_inode_flags(ni_info);
	ni->i_mode = inode->i_mode;
	
	ni->i_uid = cpu_to_le32(inode->i_uid);
	ni->i_gid = cpu_to_le32(inode->i_gid);
	ni->i_link_counts = cpu_to_le32(inode->i_nlink);
	ni->i_size = cpu_to_le64(inode->i_size);
	ni->i_atime = cpu_to_le32(inode->i_atime.tv_sec);
	ni->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
	ni->i_mtime = cpu_to_le32(inode->i_mtime.tv_sec);

	ni->i_blocks = cpu_to_le32(inode->i_blocks);
	ni->i_dtime = cpu_to_le32(ni_info->i_dtime);
	ni->i_flags = cpu_to_le32(ni_info->i_flags);
	ni->i_file_acl = cpu_to_le32(ni_info->i_file_acl);
	if (!S_ISREG(inode->i_mode))
		ni->i_dir_acl = cpu_to_le32(ni_info->i_dir_acl);
		
	ni->i_generation = cpu_to_le32(inode->i_generation);
	
	ni_info->i_state &= ~NVMM_STATE_NEW;
	
	return err;
}



/*
 * input :
 * @dentry : vfs dentry
 * @attr : 
 *
 * returns :
 *
 *
 */
int nvmm_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct nvmm_inode *ni = nvmm_get_inode(inode->i_sb,inode->i_ino);
	int error = 0;

	if(!ni){
		nvmm_error(inode->i_sb, __FUNCTION__, "inode don't exist\n");
		return -EACCES;
	}

	error = inode_change_ok(inode, attr);
	if (error){
		nvmm_error(inode->i_sb, __FUNCTION__, "inode change to wrong state\n");
		return error;
	}

	if (attr->ia_valid & ATTR_SIZE && (attr->ia_size != inode->i_size || ni->i_flags & cpu_to_le32(NVMM_EOFBLOCKS_FL))) {
		error = nvmm_setsize(inode, attr->ia_size);
		if (error){
			nvmm_error(inode->i_sb, __FUNCTION__, "inode set_size wrong\n");
			return error;
		}
	}

	setattr_copy(inode, attr);
	if (attr->ia_valid & ATTR_MODE){
		error = nvmm_acl_chmod(inode);
		if(error){
			nvmm_error(inode->i_sb, __FUNCTION__, "inode wrong\n");
		}
	}

	error = nvmm_update_inode(inode);
	if(error){
		nvmm_error(inode->i_sb, __FUNCTION__, "update inode wrong\n");
	}

	return error;
}

const struct address_space_operations nvmm_aops = {
	.readpage	= nvmm_readpage,
	.direct_IO	= nvmm_direct_IO,
};

const struct address_space_operations nvmm_aops_xip = {
	.get_xip_mem	= nvmm_get_xip_mem,
};

