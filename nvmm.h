/*
 * linux/fs/nvmm/nvmm.h
 * 
 * Copyright (C) 2013 College of Computer Science,
 * Chonqing University
 *
 */

#ifndef __NVMM_H
#define __NVMM_H

#include <linux/buffer_head.h>
#include "nvmm_fs.h"
#include <linux/types.h>
#include <linux/crc32.h>
#include <linux/mutex.h>
#include "wprotect.h"
#include <linux/spinlock.h>

#define MAX_DIR_SIZE        (1UL << 21) // 2M

#define MAX_FILE_SIZE       (1UL << 35) // 32G

/*
 * Debug Code
 */
#define nvmm_info(s, args...)		pr_info(s, ## args)
#define nvmm_trace()                printk(KERN_INFO"%s:%s\n",__FILE__,__FUNCTION__)
#define nvmm_dbg(s, args...)		pr_debug(s, ## args)
#define nvmm_warn(s, args...)		pr_warning(s, ## args)
#define nvmm_err(sb, s, args...)	nvmm_error_mng(sb, s, ## args)


#define clear_opt(o, opt)	(o &= ~NVMM_MOUNT_##opt)
#define set_opt(o, opt)		(o |= NVMM_MOUNT_##opt)
#define test_opt(sb, opt)	(NVMM_SB(sb)->s_mount_opt & NVMM_MOUNT_##opt)
//static DEFINE_SPINLOCK(superblock_lock);
/* Function Prototypes */
extern int nvmm_get_and_update_block(struct inode *inode, sector_t iblock, struct buffer_head *bh, int create);
extern void nvmm_error_mng(struct super_block * sb, const char * fmt, ...);

/* file.c */
extern ssize_t nvmm_direct_IO(int rw, struct kiocb *iocb,
			  const struct iovec *iov,
			  loff_t offset, unsigned long nr_segs);

//change mutex to spinlock
struct nvmm_inode_info{
	__u32	i_file_acl;
	__u32	i_flags;
	__u32	i_dir_acl;

	__u32	i_dtime;
	__u16	i_state;
	__le64  i_pg_addr;      /* File page table */
	atomic_t i_p_counter;	/* process num counter */
	void	*i_virt_addr;	/* inode's virtual address */
//	struct mutex truncate_mutex;
//	struct mutex i_meta_mutex;

	spinlock_t i_meta_spinlock;
	spinlock_t truncate_spinlock;
//
	struct inode	vfs_inode;
};

/*
 * Inode dynamic state flags
 */
#define NVMM_STATE_NEW			0x00000001 /* inode is newly created */

/*
 * OK,these declarations are also in <linux/kernel.h> but none of the 
 * nvmm source programs needs to include it so they are duplicated here
 */

static inline struct nvmm_inode_info *NVMM_I(struct inode *inode)
{
	return container_of(inode, struct nvmm_inode_info, vfs_inode);
}
 

/*
 * nvmm mount options
 */
struct nvmm_mount_options {
    unsigned long s_mount_opt;
    uid_t         s_resuid;
    gid_t         s_resgid;
};


//!the nvmmfs super block in MEMORY 
/*!
  This is the initial version, more fields can be added later when necessary
 */
struct nvmm_sb_info {
	void          *virt_addr;   //!< the filesystem's mount addr
	phy_addr_t    phy_addr;     //!< the filesystem's phy addr             
	unsigned long num_inodes;   //!< total inodes of the fs(%5 of the whole size of nvm)
	unsigned long blocksize;
	int s_inode_size;
	unsigned long bpi;
	unsigned long initsize;     //!< initial size of the fs
	unsigned long s_mount_opt;  //!< @TODO:can be what?
	kuid_t uid;                 //!< mount uid for root directory
	kgid_t gid;                 //!< mount gid for root directory
	umode_t mode;               //!< mount mode for root directory
	atomic_t next_generation;
//	struct mutex s_lock;
	spinlock_t s_lock;
	spinlock_t inode_spinlock;
	struct inode *consistency_i;
};


static inline struct nvmm_sb_info * NVMM_SB(struct super_block * sb)
{
    return sb->s_fs_info;
}

//! get the physaddr of the inode and block from the offset
/*!
  <long-description>

  \param sb:    the vfs superblock, which contains a pointer to nvmm_super_block_info
  \param offset:    the source offset(inode or block)
  \return <ReturnValue>: the physaddr of the interesting item
*/
static inline unsigned long nvmm_offset_to_phys(struct super_block *sb,unsigned long offset)
{
    struct nvmm_sb_info *nsi = NVMM_SB(sb);
    return (unsigned long)nsi->phy_addr + offset;
}

static inline unsigned long nvmm_phys_to_offset(struct super_block *sb,phy_addr_t phys)
{
    struct nvmm_sb_info *nsi = NVMM_SB(sb);
    return phys - nsi->phy_addr;
}


/* Mask out flags that are inappropriate for the given type of inode. */
static inline __le32 nvmm_mask_flags(umode_t mode, __le32 flags)
{
	flags &= cpu_to_le32(NVMM_FL_INHERITED);
	if (S_ISDIR(mode))
		return flags;
	else if (S_ISREG(mode))
		return flags & cpu_to_le32(NVMM_REG_FLMASK);
	else
		return flags & cpu_to_le32(NVMM_OTHER_FLMASK);
}

/*
 * ioctl commands
 */
#define	NVMM_IOC_GETFLAGS		FS_IOC_GETFLAGS
#define	NVMM_IOC_SETFLAGS		FS_IOC_SETFLAGS
#define	NVMM_IOC_GETVERSION		FS_IOC_GETVERSION
#define	NVMM_IOC_SETVERSION		FS_IOC_SETVERSION

/*
 * ioctl commands in 32 bit emulation
 */
#define NVMM_IOC32_GETFLAGS		FS_IOC32_GETFLAGS
#define NVMM_IOC32_SETFLAGS		FS_IOC32_SETFLAGS
#define NVMM_IOC32_GETVERSION		FS_IOC32_GETVERSION
#define NVMM_IOC32_SETVERSION		FS_IOC32_SETVERSION

/*
 * Inode flags (GETFLAGS/SETFLAGS)
 */
#define NVMM_FL_USER_VISIBLE		FS_FL_USER_VISIBLE	/* User visible flags */
#define NVMM_FL_USER_MODIFIABLE		FS_FL_USER_MODIFIABLE	/* User modifiable flags */
static inline int nvmm_calc_checksum(u8 *data, int n)
{
	u32 crc = 0;
	crc = crc32(~0, (__u8 *)data + sizeof(__le32), n - sizeof(__le32));
	if (*((__le32 *)data) == cpu_to_le32(crc))
		return 0;
	else
		return 1;
}

static inline struct nvmm_super_block * 
nvmm_get_super(struct super_block * sb)
{
    struct nvmm_sb_info *sbi = NVMM_SB(sb);
    return (struct nvmm_super_block*)sbi->virt_addr;
}
static inline struct nvmm_super_block *
nvmm_get_redund_super(struct super_block *sb)
{
	struct nvmm_sb_info *sbi = NVMM_SB(sb);
	return (struct nvmm_super_block *)(sbi->virt_addr + NVMM_SB_SIZE);
}
static inline unsigned long nvmm_get_free_list(struct super_block *sb)
{
    struct nvmm_super_block * nsb = nvmm_get_super(sb);
    return nsb->s_free_block_start;
}


//! Get the virtural address of the  inode of the nvmmfs
/*!
  <long-description>
  the calculation is based on the offset to the fs
  \param sb:    the vfs super block
  \param offset:    the offset of the intersted inode within the fs
*/
/* static inline struct nvmm_inode * */
/* nvmm_get_inode(struct super_block *sb,u64 offset) */
/* { */
/*     struct nvmm_super_block *nsb = nvmm_get_super(sb); */
/*     return (struct nvmm_inode *)((void *)nsb + offset); */
/* } */


static inline struct nvmm_inode * 
nvmm_get_inode(struct super_block * sb, u64 ino)
{
    struct nvmm_super_block * nsb = nvmm_get_super(sb);
    struct nvmm_inode *ni;
    ni = (struct nvmm_inode *)((void *)nsb + PAGE_SIZE + ((ino - 1) << NVMM_INODE_BITS));
    return ino ? ni : NULL;
}



static inline unsigned long
nvmm_get_inode_phy_addr(struct super_block * sb, u64 ino)
{
    struct nvmm_sb_info * nsi;
    if (ino <= 0)
        return -BADINO;
    nsi = NVMM_SB(sb);
    return (unsigned long)(nsi->phy_addr + PAGE_SIZE  + ((ino - NVMM_ROOT_INO) << NVMM_INODE_BITS ));
}


static inline u64 
nvmm_get_inodenr(struct super_block *sb, unsigned long phy_addr)
{
    struct nvmm_sb_info *nsi = NVMM_SB(sb);
    return (((phy_addr - nsi->phy_addr - PAGE_SIZE) >> NVMM_INODE_BITS) + NVMM_ROOT_INO);
}

static inline void *
nvmm_get_block(struct super_block *sb, u64 block)
{
	struct nvmm_super_block *ps = nvmm_get_super(sb);
	return block ? ((void *)ps + block) : NULL;
}

static inline void check_eof_blocks(struct inode *inode, loff_t size)
{
	struct nvmm_inode *ni = nvmm_get_inode(inode->i_sb, inode->i_ino);

	if(unlikely(!ni))
		return;
	if((ni->i_flags & cpu_to_le32(NVMM_EOFBLOCKS_FL)) &&
			size + inode->i_sb->s_blocksize >= 
			(inode->i_blocks << inode->i_sb->s_blocksize_bits))
		ni->i_flags &= cpu_to_le32(~NVMM_EOFBLOCKS_FL);
}

/*
static void nvmm_set_blocksize(struct super_block *sb,unsigned long size)
{
    int bits = 0;
    bits = fls(size) - 1;
    sb->s_blocksize_bits = bits;
    sb->s_blocksize = (1 << bits);
}
*/

/*
 * Function prototypes 
 */
extern void nvmm_error(struct super_block *sb, const char * function,
                       const char * fmt,...);
extern void nvmm_msg(struct super_block *sb, const char * function,
                     const char * fmt,...);

/* acl.c */

/* balloc.c */
extern void nvmm_init_free_inode_list_offset(struct nvmm_super_block *ps,void *sbi_virt_addr);
extern void nvmm_init_free_block_list_offset(struct nvmm_super_block *ps,void *sbi_virt_addr);
extern int nvmm_new_block(struct super_block *sb, phys_addr_t *physaddr, 
                          int zero /* fill 0 if zero is set */, int num);
extern struct page * nvmm_new_page(struct super_block *sb, int zero);
extern void nvmm_free_block(struct super_block *sb, unsigned long blocknr);
extern unsigned long nvmm_count_free_blocks(struct super_block *sb);
extern unsigned long nvmm_offset_to_phys(struct super_block *sb,unsigned long offset);
extern inline unsigned long nvmm_phys_to_offset(struct super_block *sb,phy_addr_t phys);
extern unsigned long nvmm_get_zeroed_page(struct super_block *sb);

/* inode.c */
extern u64 nvmm_find_data_block(struct inode *inode, unsigned long file_blocknr);
extern int nvmm_alloc_blocks(struct inode *inode, int num);
extern int nvmm_update_inode(struct inode *inode);
extern struct inode *nvmm_iget(struct super_block *sb, unsigned long ino);
extern void nvmm_evict_inode(struct inode * inode);
extern void nvmm_get_inode_flags(struct nvmm_inode_info *ni_info);
extern struct inode *nvmm_new_inode(struct inode *dir, umode_t mode, const struct qstr *qstr);
extern int nvmm_write_inode(struct inode *inode, struct writeback_control *wbc);
extern void nvmm_dirty_inode(struct inode *inode);
extern int nvmm_notify_change(struct dentry *dentry, struct iattr *attr);
extern void nvmm_set_inode_flags(struct inode *inode);
extern void nvmm_free_inode(struct inode *inode);
/* pagtable.c */
extern int nvmm_establish_mapping(struct inode *inode);
extern int nvmm_insert_page(struct super_block *sb, struct inode *inode, struct page *pg);
extern int nvmm_destroy_mapping(struct inode *inode);
extern void nvmm_rm_pg_table(struct super_block *sb, u64 ino);
extern pud_t* nvmm_get_pud(struct super_block *sb, u64 ino);
extern int nvmm_init_pg_table(struct super_block *sb, u64 ino);
extern int nvmm_mapping_file(struct inode *inode);
extern int nvmm_unmapping_file(struct inode *inode);
extern void nvmm_setup_pud(pud_t *pud, pmd_t *pmd);
extern void nvmm_setup_pmd(pmd_t *pmd, pte_t *pte);
extern void nvmm_setup_pte(pte_t *pte, struct page *pg);
extern void nvmm_rm_pte_range(struct super_block *sb, pmd_t *pmd);
extern void nvmm_rm_pmd_range(struct super_block *sb, pud_t *pud);
/*
 * Inode and files operations
 */
/* dir.c */
extern const struct file_operations nvmm_dir_operations;
extern struct page *nvmm_virt_to_page(void *vaddr);
extern int nvmm_make_empty(struct inode *inode,struct inode *parent);
extern int nvmm_add_link(struct dentry *dentry,struct inode *inode);
extern int nvmm_empty_dir(struct inode *inode);
extern struct nvmm_dir_entry *nvmm_find_entry2(struct inode *dir,struct qstr *child,
	struct nvmm_dir_entry **up_nde);
extern int nvmm_delete_entry(struct nvmm_dir_entry *dir,struct nvmm_dir_entry **pdir,
	struct inode *parent);
extern ino_t nvmm_inode_by_name(struct inode *dir,struct qstr *child);
extern struct nvmm_dir_entry *nvmm_dotdot(struct inode *dir);
extern void nvmm_set_link(struct inode *dir, struct nvmm_dir_entry *de, struct inode *inode,
                          int update_times);

/* nvmalloc.c */
extern int nvmalloc_init(void);
extern void *nvmalloc(const int mode);
extern void nvfree(const void *addr);
extern int nvmap(unsigned long addr, pud_t *ppud, struct mm_struct *mm);
extern int nvmap_pmd(const unsigned long addr, pmd_t *pmd, struct mm_struct *mm);
extern int unnvmap(unsigned long addr, pud_t *ppud, struct mm_struct *mm);
extern int unnvmap_pmd(const unsigned long addr, pmd_t *pmd, struct mm_struct *mm);
extern void print_free_list(int mode);
extern void print_used_list(int mode);


/* file.c */
extern int nvmm_fsync(struct file *file, struct dentry *dentry, int datasync);
extern const struct inode_operations nvmm_file_inode_operations;
extern const struct file_operations  nvmm_file_operations;
extern const struct file_operations  nvmm_xip_file_operations;

/* inode.c */
extern const struct address_space_operations nvmm_aops;
extern const struct address_space_operations nvmm_aops_xip;
extern const struct address_space_operations nvmm_nobh_aops;

/* ioctl.c*/
extern long nvmm_ioctl(struct file *, unsigned int, unsigned long);
extern long nvmm_compat_ioctl(struct file *, unsigned int, unsigned long);
/* super.c */
extern int nvmm_statfs(struct dentry *d, struct kstatfs *buf);
/* namei.c */
extern const struct inode_operations nvmm_dir_inode_operations;
extern const struct inode_operations nvmm_special_inode_operations;
/*symlink.c*/
extern struct inode_operations nvmm_symlink_inode_operations;
extern int nvmm_page_symlink(struct inode *inode, const char *symname, int len);


//extern backing_dev_info nvmm_backing_dev_info;
#endif //__NVMM_H
