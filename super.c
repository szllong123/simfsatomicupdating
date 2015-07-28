/*
 * linux/fs/nvmm/super.c
 *
 * Copyright (C) 2013 College of Computer Science,
 * Chonqing University
 * 
 * Super block operations
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/parser.h>
#include <linux/random.h>
#include <linux/buffer_head.h>
#include <linux/exportfs.h>
//#include <linux/smp_lock.h>
#include <linux/vfs.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include "nvmm.h"

#define NVMM_SUPER_MAGIC 0xEFFB
static loff_t nvmm_max_size(int bits)
{
	loff_t res;
	res = (1UL << 35) - 1;

	if(res > MAX_LFS_FILESIZE)
		res = MAX_LFS_FILESIZE;

	nvmm_info("max file size %llu bytes\n",res);
	return res;
}

enum {
	Opt_addr, Opt_bpi, Opt_size,
	Opt_num_inodes, Opt_mode, Opt_uid,
	Opt_gid, Opt_blocksize, Opt_user_xattr,
	Opt_nouser_xattr, Opt_noprotect,
	Opt_acl, Opt_noacl, Opt_xip,
	Opt_err_cont, Opt_err_panic, Opt_err_ro,
	Opt_err
};

static const match_table_t tokens = {
	{Opt_bpi,		"physaddr=%x"},
	{Opt_bpi,		"bpi=%u"},
	{Opt_size,		"init=%s"},
	{Opt_num_inodes,	"N=%u"},
	{Opt_mode,		"mode=%o"},
	{Opt_uid,		"uid=%u"},
	{Opt_gid,		"gid=%u"},
	{Opt_blocksize,		"bs=%s"},
	{Opt_user_xattr,	"user_xattr"},
	{Opt_user_xattr,	"nouser_xattr"},
	{Opt_noprotect,		"noprotect"},
	{Opt_acl,		"acl"},
	{Opt_acl,		"noacl"},
	{Opt_xip,		"xip"},
	{Opt_err_cont,		"errors=continue"},
	{Opt_err_panic,		"errors=panic"},
	{Opt_err_ro,		"errors=remount-ro"},
	{Opt_err,		NULL},
};

static struct kmem_cache * nvmm_inode_cachep;

static inline int isdigit(int ch)
{
	return (ch >= '0') && (ch <= '9');
}
static int nvmm_show_options(struct seq_file *seq, struct dentry *root);


static struct inode *nvmm_alloc_inode(struct super_block *sb)
{
    struct nvmm_inode_info *vi = (struct nvmm_inode_info *)
        kmem_cache_alloc(nvmm_inode_cachep,GFP_NOFS);
    if (!vi) {
        return NULL;
    }
    vi->vfs_inode.i_version = 1;
    return &vi->vfs_inode;
}

static void nvmm_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head,struct inode,i_rcu);
	kmem_cache_free(nvmm_inode_cachep,NVMM_I(inode));
}

static void nvmm_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu,nvmm_i_callback);
}

static void nvmm_put_super(struct super_block *sb)
{
	struct nvmm_sb_info *nsi = NVMM_SB(sb);
	sb->s_fs_info = NULL;
	kfree(nsi);
}

int nvmm_remount(struct super_block *sb,int *mntflags,char *data)
{
	return 0;
}


static struct super_operations nvmm_sops = {
	.alloc_inode	= nvmm_alloc_inode,
	.destroy_inode	= nvmm_destroy_inode,
	.write_inode	= nvmm_write_inode,
	//.dirty_inode	= nvmm_dirty_inode,
	.evict_inode	= nvmm_evict_inode,
	.put_super	= nvmm_put_super,	
	.remount_fs	= nvmm_remount,
	.statfs		= nvmm_statfs,
	.show_options	= nvmm_show_options,
};


void nvmm_error(struct super_block * sb, const char * function,
            const char * fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    printk(KERN_ERR "NVMMFS (%s): error: %s: ", sb->s_id, function);
    vprintk(fmt, args);
    printk("\n");
    va_end(args);
}


void nvmm_msg(struct super_block * sb, const char * prefix,
            const char * fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    printk("%sNVMMFS (%s): ", prefix, sb->s_id);
    vprintk(fmt, args);
    printk("\n");
    va_end(args);
}

static void init_once(void *foo)
{
	struct nvmm_inode_info *vi = (struct nvmm_inode_info *) foo;
//	mutex_init(&vi->i_meta_mutex);
//	mutex_init(&vi->truncate_mutex);

	spin_lock_init(&vi->i_meta_spinlock);
	spin_lock_init(&vi->truncate_spinlock);
	inode_init_once(&vi->vfs_inode);
}

static int init_inodecache(void)
{
    nvmm_inode_cachep = kmem_cache_create("nvmm_inode_cache",
                        sizeof(struct nvmm_inode_info),
                        0, (SLAB_RECLAIM_ACCOUNT |
                        SLAB_MEM_SPREAD),
                        init_once);
    if (nvmm_inode_cachep == NULL)
        return -ENOMEM;
    return 0;
}

static void destory_inodecache(void)
{
    kmem_cache_destroy(nvmm_inode_cachep);
}


//! Calculate the checksum of the super block
/*!
  <long-description>

  \param void:  
  \return <ReturnValue>:return 0 for test purpose
*/
int calc_super_checksum(void)
{
    return 0;
}

static phys_addr_t get_phys_addr(void **data)
{
	phys_addr_t phys_addr;
	char *options = (char *) *data;
	unsigned long long ulltmp;
	char *end;
	char org_end;
	int err;

	if (!options || strncmp(options, "physaddr=", 9) != 0)
		return (phys_addr_t)ULLONG_MAX;
	options += 9;
	end = strchr(options, ',') ?: options + strlen(options);
	org_end = *end;
	*end = '\0';
	err = kstrtoull(options, 0, &ulltmp);
	*end = org_end;
	options = end;
	phys_addr = (phys_addr_t)ulltmp;
	if (err) {
		printk(KERN_ERR "Invalid phys addr specification: %s\n",
		       (char *) *data);
		return (phys_addr_t)ULLONG_MAX;
	}
	if (phys_addr & (PAGE_SIZE - 1)) {
		printk(KERN_ERR "physical address 0x%16llx for pramfs isn't "
			  "aligned to a page boundary\n",
			  (u64)phys_addr);
		return (phys_addr_t)ULLONG_MAX;
	}
	if (*options == ',')
		options++;
	*data = (void *) options;
	return phys_addr;
}


static int nvmm_parse_options(char *options, struct nvmm_sb_info *sbi,
			      bool remount)
{
	char *p, *rest;
	substring_t args[MAX_OPT_ARGS];
	int option;

	if (!options)
		return 0;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_addr:
			if (remount)
				goto bad_opt;
			/* physaddr managed in get_phys_addr() */
			break;
		case Opt_bpi:
			if (remount)
				goto bad_opt;
			if (match_int(&args[0], &option))
				goto bad_val;
			//sbi->bpi = option;
			break;
		case Opt_uid:
			if (remount)
				goto bad_opt;
			if (match_int(&args[0], &option))
				goto bad_val;
			sbi->uid = make_kuid(current_user_ns(), option);
			if (!uid_valid(sbi->uid))
				goto bad_val;
			break;
		case Opt_gid:
			if (match_int(&args[0], &option))
				goto bad_val;
			sbi->gid = make_kgid(current_user_ns(), option);
			if (!gid_valid(sbi->gid))
				goto bad_val;
			break;
		case Opt_mode:
			if (match_octal(&args[0], &option))
				goto bad_val;
			sbi->mode = option & 01777U;
			break;
		case Opt_size:
			if (remount)
				goto bad_opt;
			/* memparse() will accept a K/M/G without a digit */
			if (!isdigit(*args[0].from))
				goto bad_val;
			sbi->initsize = memparse(args[0].from, &rest);
			break;
		case Opt_num_inodes:
			if (remount)
				goto bad_opt;
			if (match_int(&args[0], &option))
				goto bad_val;
				sbi->num_inodes = option;
				break;
		case Opt_blocksize:
			if (remount)
				goto bad_opt;
			/* memparse() will accept a K/M/G without a digit */
			if (!isdigit(*args[0].from))
				goto bad_val;
			sbi->blocksize = memparse(args[0].from, &rest);
			if (sbi->blocksize < NVMM_MIN_BLOCK_SIZE ||
				sbi->blocksize > NVMM_MAX_BLOCK_SIZE ||
				!is_power_of_2(sbi->blocksize))
				goto bad_val;
			break;
default: {
			goto bad_opt;
		}
		}
	}

	return 0;

bad_val:
	printk(KERN_ERR "Bad value '%s' for mount option '%s'\n", args[0].from,
	       p);
	return -EINVAL;
bad_opt:
	printk(KERN_ERR "Bad mount option: \"%s\"\n", p);
	return -EINVAL;
}

static void nvmm_set_blocksize(struct super_block *sb, unsigned long size)
{
	int bits;

	/*
	 * We've already validated the user input and the value here must be
	 * between PRAM_MAX_BLOCK_SIZE and PRAM_MIN_BLOCK_SIZE
	 * and it must be a power of 2.
	 */
	bits = fls(size) - 1;
	sb->s_blocksize_bits = bits;
	sb->s_blocksize = (1<<bits);
}

static struct nvmm_inode *nvmm_init(struct super_block *sb, unsigned long size)
{
	unsigned long bpi, num_inodes, blocksize, num_blocks;
	u64 free_blk_start;
	struct nvmm_inode *root_i;
	struct nvmm_super_block *super;
	struct nvmm_sb_info *sbi = NVMM_SB(sb);

	nvmm_info("creating an empty nvmmfs of size %lu\n", size);
	sbi->virt_addr = __va(sbi->phy_addr);
	nvmm_info("The physaddr:0x%lx,virtual address: 0x%lx\n",
        (unsigned long)sbi->phy_addr,(unsigned long)sbi->virt_addr);

	if (!sbi->virt_addr) {
		printk(KERN_ERR "get the virtual address of zone failed\n");
		return ERR_PTR(-EINVAL);
	}

	if (!sbi->blocksize)
		blocksize = PAGE_SIZE;
	else
		blocksize = sbi->blocksize;

	nvmm_set_blocksize(sb, blocksize);
	blocksize = sb->s_blocksize;

	if (sbi->blocksize && sbi->blocksize != blocksize)
		sbi->blocksize = blocksize;

	if (size < blocksize) {
		printk(KERN_ERR "size smaller then block size\n");
		return ERR_PTR(-EINVAL);
	}

	if (!sbi->bpi)
		/*
		 * default is that 1% of the filesystem is
		 * devoted to the inode table
		 */
		bpi = 100 * NVMM_INODE_SIZE;
	else
		bpi = sbi->bpi;

	if (!sbi->num_inodes)
		num_inodes = size / bpi;
	else
		num_inodes = sbi->num_inodes;

	/*
	 * up num_inodes such that the end of the inode table
	 * (and start of bitmap) is on a block boundary
	 */
	free_blk_start = (PAGE_SIZE) + (num_inodes << NVMM_INODE_BITS);
	if (free_blk_start & (blocksize - 1))
		free_blk_start = (free_blk_start + blocksize) &
			~(blocksize -1);
	num_inodes = (free_blk_start - (PAGE_SIZE)) >> NVMM_INODE_BITS;

	if (sbi->num_inodes && num_inodes != sbi->num_inodes)
		sbi->num_inodes = num_inodes;

	num_blocks = (size - free_blk_start) >> sb->s_blocksize_bits;

	if (!num_blocks) {
		printk(KERN_ERR "num blocks equals to zero\n");
		return ERR_PTR(-EINVAL);
	}


	nvmm_info("blocksize %lu, num inodes %lu, num blocks %lu\n",
		  blocksize, num_inodes, num_blocks);
	nvmm_info("free block start 0x%08x\n",(unsigned int)free_blk_start);
	
	super = nvmm_get_super(sb);

	/* clear out super-block and inode table */
	memset(super, 0, size);

	super->s_size = cpu_to_le64(size);
	super->s_blocksize = cpu_to_le32(blocksize);
	super->s_inode_count = cpu_to_le64(num_inodes);
	super->s_block_count = cpu_to_le64(num_blocks);
	super->s_free_inode_count = cpu_to_le64(num_inodes - 1);
	super->s_free_block_count = cpu_to_le64(num_blocks);
	super->s_magic = cpu_to_le16(NVMM_SUPER_MAGIC);
    	super->s_inode_start = cpu_to_le64(PAGE_SIZE); 
	super->s_block_start = cpu_to_le64(free_blk_start);
    	super->s_free_inode_start = cpu_to_le64(NVMM_ROOT_INODE_OFFSET + NVMM_INODE_SIZE);
    	super->s_free_block_start = cpu_to_le64(free_blk_start);
    	nvmm_init_free_inode_list_offset(super,sbi->virt_addr);
    	nvmm_init_free_block_list_offset(super,sbi->virt_addr);
	nvmm_sync_super(super);
    
	root_i = nvmm_get_inode(sb, NVMM_ROOT_INO);

	root_i->i_mode = cpu_to_le16(sbi->mode | S_IFDIR);
	root_i->i_uid = cpu_to_le32(sbi->uid);
	root_i->i_gid = cpu_to_le32(sbi->gid);
	root_i->i_link_counts = cpu_to_le16(2);
	/*establish pagetable for root inode*/
	nvmm_init_pg_table(sb, NVMM_ROOT_INO);
	nvmm_sync_inode(root_i);

	return root_i;
}

static inline void set_default_opts(struct nvmm_sb_info *sbi)
{
	set_opt(sbi->s_mount_opt, ERRORS_CONT);
}

static int nvmm_fill_super(struct super_block *sb, void *data, int silent)
{
    struct inode *root_i;
    struct nvmm_inode *root_nvmmi;
    struct nvmm_super_block *super, *super_redund;
    struct nvmm_sb_info *sbi = NULL;
    unsigned long initsize, blocksize;
    u32 random = 0;
    int retval = 0;
    nvmm_trace();
    sbi = kzalloc(sizeof(struct nvmm_sb_info),GFP_KERNEL);
    if (!sbi) {
        return -ENOMEM;
    }
    sb->s_fs_info = sbi;        //!< initialize the vfs super block with the nvmm_sb_info

    set_default_opts(sbi);
	
//	mutex_init(&sbi->s_lock);

	spin_lock_init(&sbi->s_lock);
	spin_lock_init(&sbi->inode_spinlock);

    sbi->phy_addr = get_phys_addr(&data);
    if(sbi->phy_addr == (phys_addr_t)ULLONG_MAX)
	    goto out;

    get_random_bytes(&random, sizeof(u32));
    atomic_set(&sbi->next_generation, random);

    /* Init with default values */
	sbi->mode = (S_IRWXUGO | S_ISVTX);
	sbi->uid = current_fsuid();
	sbi->gid = current_fsgid();
    
    if (nvmm_parse_options(data, sbi, 0))
		goto out;
    
    initsize = sbi->initsize;
    
    if (initsize) {
        root_nvmmi = nvmm_init(sb,initsize);
        if (IS_ERR(root_nvmmi)) {
            goto out;
        }
        super = nvmm_get_super(sb);
        goto setup_sb;
    }

    nvmm_info("Check physaddr 0x%lx for nvmmfs image!\n",(unsigned long)sbi->phy_addr);

    sbi->virt_addr = __va(sbi->phy_addr);
    if(!sbi->virt_addr){
    	printk(KERN_ERR "get nvmmfs start virtual address failed\n");
	goto out;
    }

    super = nvmm_get_super(sb);
    super_redund = nvmm_get_redund_super(sb);

    /* if(le16_to_cpu(super->s_magic) != NVMM_SUPER_MAGIC){ */
	/*     if(le16_to_cpu(super_redund->s_magic) != NVMM_SUPER_MAGIC){ */
	/* 	    if(!silent) */
	/* 		    printk(KERN_ERR "can't find a valid nvmmfs pratition\n"); */
	/* 	    goto out; */
	/*     }else{ */
	/* 	    nvmm_warn("error in super block : try to repair it with the redundant copy"); */

	/* 	    nvmm_memunlock_super(sb,super); */
	/* 	    memcpy(super, super_redund, NVMM_SB_SIZE); */
	/* 	    nvmm_memlock_super(sb, super); */
	    
	/*     } */
    /* } */

    /* if(nvmm_calc_checksum((u8 *)super, NVMM_SB_SIZE)){ */
	/*     if(nvmm_calc_checksum((u8 *)super_redund, NVMM_SB_SIZE)){ */
	/* 	    printk(KERN_ERR "checksum errorin super block\n"); */
	/* 	    goto out; */
	/*     }else{ */
	/* 	    nvmm_warn("error in super block : tyr to repair it with the redundant copy\n"); */

	/* 	    nvmm_memunlock_super(sb, super); */
	/* 	    memcpy(super, super_redund, NVMM_SB_SIZE); */
	/* 	    nvmm_memlock_super(sb, super); */
	/*     } */
    /* } */

    blocksize = le32_to_cpu(super->s_blocksize);
    nvmm_set_blocksize(sb,blocksize);

    initsize = le64_to_cpu(super->s_size);
    nvmm_info("nvmmfs image appears to be %lu KB in size\n", initsize>>10);
	nvmm_info("blocksize %lu\n", blocksize);

    
    
 setup_sb:                      /* Left out many fields just to test! */
    sb->s_magic = super->s_magic;
    sb->s_op = &nvmm_sops;           //!< Can it be? For test purpose!
    sb->s_maxbytes = nvmm_max_size(sb->s_blocksize_bits);
    sb->s_max_links =  NVMM_LINK_MAX;
    sb->s_flags |= MS_NOSEC;
    root_i = nvmm_iget(sb,NVMM_ROOT_INO);
    nvmm_make_empty(root_i,root_i);
    if (IS_ERR(root_i)) {
        retval = PTR_ERR(root_i);
        goto out;
    }
    sb->s_root = d_make_root(root_i);
    if (!sb->s_root) {
        printk(KERN_ERR"get nvmmfs root inode failed!\n");
        retval = -ENOMEM;
        goto out;
    }

	//create a consistency_i inode and set its mode for file
	NVMM_SB(sb)->consistency_i = nvmm_new_inode(root_i, 0, NULL);
	NVMM_SB(sb)->consistency_i->i_mode = cpu_to_le16(sbi->mode | S_IFREG);
	nvmm_alloc_blocks(NVMM_SB(sb)->consistency_i, (10 * PMD_SIZE + sb->s_blocksize - 1) >> sb->s_blocksize_bits);

    retval = 0;  
    return retval;
 out:
    if (sbi->virt_addr) {       
        nvmm_info("The zone virtual address not empty!\n");
    }
    kfree(sbi);                 //!< and also free the sbi
    return retval;
}

/*
 * NVMM_SUPER_MAGIC should be add to /include/uapi/linux/magic.h
 * buf->f_namelen may be wrong
 *
 * input :
 * @d : root dentry
 * output :
 * buf : get info about fs
 * returns :
 * 0 if success, in fact, it will always return 0
 * this function scheduled when command df was used
 */
int nvmm_statfs(struct dentry *d, struct kstatfs *buf)
{
	struct super_block *sb = d->d_sb;
	struct nvmm_super_block *ns = nvmm_get_super(sb);
	buf->f_type = NVMM_SUPER_MAGIC;			//NVMM_SUPER_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = le64_to_cpu(ns->s_block_count);
	buf->f_bfree = buf->f_bavail = nvmm_count_free_blocks(sb);
	buf->f_files = le64_to_cpu(ns->s_inode_count);
	buf->f_ffree = le64_to_cpu(ns->s_free_inode_count);
	buf->f_namelen = 128;
	return 0;
}


/*
 * input :
 * @seq :
 * @root
 * returns :
 * 0 if success
 * this function is scheduled when command mount is used
 */
static int nvmm_show_options(struct seq_file *seq, struct dentry *root)
{
	struct nvmm_sb_info *sbi = NVMM_SB(root->d_sb);

	seq_printf(seq, ".physaddr=0x%016llx", (u64)sbi->phy_addr);
	if (sbi->initsize)
		seq_printf(seq, ",init=%luk", sbi->initsize >> 10);
	if (sbi->blocksize)
		seq_printf(seq, ",bs=%lu", sbi->blocksize);
	if (sbi->bpi)
		seq_printf(seq, ",bpi=%lu", sbi->bpi);
	if (sbi->num_inodes)
		seq_printf(seq, ",N=%lu", sbi->num_inodes);
	if (sbi->mode != (S_IRWXUGO | S_ISVTX))
		seq_printf(seq, ",mode=%03o", sbi->mode);
	if (!uid_eq(sbi->uid, GLOBAL_ROOT_UID))
		seq_printf(seq, ",uid=%u",
				from_kuid_munged(&init_user_ns, sbi->uid));
	if (!gid_eq(sbi->gid, GLOBAL_ROOT_GID))
		seq_printf(seq, ",gid=%u",
				from_kgid_munged(&init_user_ns, sbi->gid));
	if (test_opt(root->d_sb, ERRORS_RO))
		seq_puts(seq, ",errors=remount-ro");
	if (test_opt(root->d_sb, ERRORS_PANIC))
		seq_puts(seq, ",errors=panic");

#ifdef CONFIG_NVMMFS_XATTR
	/* user xattr not enabled by default */
	if (test_opt(root->d_sb, XATTR_USER))
		seq_puts(seq, ",user_xattr");
#endif

#ifdef CONFIG_NVMMFS_POSIX_ACL
	/* acl not enabled by default */
	if (test_opt(root->d_sb, POSIX_ACL))
		seq_puts(seq, ",acl");
#endif

#ifdef CONFIG_NVMMFS_XIP
	/* xip not enabled by default */
	if (test_opt(root->d_sb, XIP))
		seq_puts(seq, ",xip");
#endif

	return 0;
}
static struct dentry *nvmm_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
    nvmm_trace();
	return mount_nodev(fs_type, flags, data, nvmm_fill_super);
}



static struct file_system_type nvmm_fs_type = {
    .owner      = THIS_MODULE,
    .name       = "nvmmfs",
    .mount      = nvmm_mount,
    .kill_sb    = kill_anon_super,
};
MODULE_ALIAS_FS("nvmmfs");


int init_nvmm_fs(void)
{

	int rc = 0;
    nvmm_trace();
    rc = nvmalloc_init();

    rc = init_inodecache();
	if(rc)
		goto out;

    rc = register_filesystem(&nvmm_fs_type);
    if (rc)
        goto out;
    return 0;
    
    out:

	return rc;
}


static void __exit exit_nvmm_fs(void)
{
    nvmm_trace();
    unregister_filesystem(&nvmm_fs_type);
    destory_inodecache();
}


module_init(init_nvmm_fs)
module_exit(exit_nvmm_fs)

MODULE_AUTHOR("College of Computer Science, Chongqing University");
MODULE_DESCRIPTION("Non-Voliatle Main Memory File System");
MODULE_LICENSE("GPL");
