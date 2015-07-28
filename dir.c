/*
 * linux/fs/nvmm/dir.c
 *
 * Copyright (C) 2013 College of Computer Science,
 * Chonqing University
 *
 * inode operations
 *
 */

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/list.h>
#include <asm/page.h>
#include "nvmm.h"

static inline unsigned nvmm_chunk_size(struct inode *inode)
{
	return inode->i_sb->s_blocksize;
}

/*
  compare the 'name' with the dentry 'de'
 */
static inline int nvmm_match(int len,const char* const name,
                             struct nvmm_dir_entry *de)
{
    if(len != de->name_len)
        return 0;
    if(!de->inode)
        return 0;
    return !memcmp(name,de->name,len);    
}

/*
 * input :
 * @p : type of struct nvmm_dir_entry
 * returns :
 * the next dentry
 */

static inline struct nvmm_dir_entry * nvmm_next_entry(struct nvmm_dir_entry *p)
{
	return (struct nvmm_dir_entry *)((char *)p + cpu_to_le16(p->rec_len));
    /*rec_len is the total length(in bytes) of dentry which includes 'inode' ,'rec_len',
      'name_len','file_type','name'
     */
}

static unsigned char nvmm_filetype_table[NVMM_FT_MAX] = {
	[NVMM_FT_UNKNOWN]		= DT_UNKNOWN,
	[NVMM_FT_REG_FILE]	= DT_REG,
	[NVMM_FT_DIR]		= DT_DIR,
	[NVMM_FT_CHRDEV]		= DT_CHR,
	[NVMM_FT_BLKDEV]		= DT_BLK,
	[NVMM_FT_FIFO]		= DT_FIFO,
	[NVMM_FT_SOCK]		= DT_SOCK,
	[NVMM_FT_SYMLINK]		= DT_LNK,
};

#define S_SHIFT 12
static unsigned char nvmm_type_by_mode[S_IFMT >> S_SHIFT] = {
	[S_IFREG >> S_SHIFT]	= NVMM_FT_REG_FILE,
	[S_IFDIR >> S_SHIFT]	= NVMM_FT_DIR,
	[S_IFCHR >> S_SHIFT]	= NVMM_FT_CHRDEV,
	[S_IFBLK >> S_SHIFT]	= NVMM_FT_BLKDEV,
	[S_IFIFO >> S_SHIFT]	= NVMM_FT_FIFO,
	[S_IFSOCK >> S_SHIFT]	= NVMM_FT_SOCK,
	[S_IFLNK >> S_SHIFT]	= NVMM_FT_SYMLINK,
};

static int
nvmm_readdir(struct file *file, struct dir_context *ctx)
{
	loff_t pos = ctx->pos;
	int err = 0;
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	unsigned long offset = pos && ~PAGE_CACHE_MASK;
	unsigned char *types = NULL;
	char *start_addr, *end_addr;
	struct nvmm_dir_entry *nde;

	types = nvmm_filetype_table;
	
	/*establish mapping*/
	err = nvmm_establish_mapping(inode);


	if(pos >= inode->i_size)
		goto final;

	if(file->f_version != inode->i_version){
		if(offset){
			offset = 0;
			ctx->pos = (loff_t)(NVMM_I(inode)->i_virt_addr);
		}
		file->f_version = inode->i_version;
	}
	start_addr = NVMM_I(inode)->i_virt_addr;
	end_addr = start_addr + inode->i_size;
	if(start_addr >= end_addr)
		goto final;
	nde = (struct nvmm_dir_entry *)start_addr;
	for(;(char *)nde < end_addr;nde =  nvmm_next_entry(nde)){
		if(nde->rec_len == 0){
			nvmm_error(sb, __FUNCTION__, "zero-length directory entry\n");
			err = -EIO;
			goto final;
		}
		if(nde->inode){
			unsigned char d_type = DT_UNKNOWN;
			if(types && nde->file_type < NVMM_FT_MAX)
				d_type = types[nde->file_type];
			if(!dir_emit(ctx, nde->name, nde->name_len, le64_to_cpu(nde->inode),d_type))
				goto final;
		}
		ctx->pos += le16_to_cpu(nde->rec_len);
	}

final:
	/*destroy table mapping*/
	err = nvmm_destroy_mapping(inode);

	return err;
}

/*
 * input : 
 * @dir : vfs inode, parent directory
 * @child : type of struct qstr, information about child
 * @returns :
 * dentry correspond to child if find,else NULL
 *
 * finds an entry in the specified directory with the wanted name.
 
struct nvmm_dir_entry *nvmm_find_entry(struct inode *dir, struct qstr *child, struct nvmm_dir_entry *up_nde)
{
	const char *name = child->name;
	int namelen = child->len;
	unsigned long  n = i_size_read(dir);
	void *start, *end;
	struct nvmm_dir_entry *nde;
	int err = 0;

	//establish mappong
	err = nvmm_establish_mapping(dir);
	start = NVMM_I(dir)->i_virt_addr;
	
	if(n <= 0)
		goto out;

	up_nde = nde = (struct nvmm_dir_entry *)start;
	end = start + n;

	while((void *)nde < end)
	{
		if(nde->rec_len == 0){
			nvmm_error(dir->i_sb,__FUNCTION__,"zero-length directory entry\n");
			goto out;
		}
		if(nvmm_match(namelen, name, nde))
			goto found;
		up_nde = nde;
		nde = nvmm_next_entry(nde);
	}
out:
	//destroy table mapping
	err = nvmm_destroy_mapping(dir);

	return NULL;

found:
	//destroy table mapping
	err = nvmm_destroy_mapping(dir);

	return nde;
}*/

/*
  @nvmm_virt_to_page:return page structure of the given virtual address 
 */
struct page *nvmm_virt_to_page(void *vaddr)
{
    struct mm_struct *mm;
    unsigned long va,pa;
    pgd_t *pgd;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;

    mm = &init_mm;
    va = (unsigned long)(vaddr);
    pgd = pgd_offset(mm, va);
    pud = pud_alloc(mm, pgd, va);
    pmd = pmd_alloc(mm, pud, va);
    pte = pte_offset_map(pmd, va);
    pa = ((pte_val(*pte) & 0x0fffffffffffffff) & PAGE_MASK);
    return pfn_to_page(pa >> PAGE_SHIFT);
}

/*
  find the dentry,and also find the dentry's previous's dentry,that is 'up_nde'
 */
struct nvmm_dir_entry *nvmm_find_entry2(struct inode *dir, struct qstr *child, struct nvmm_dir_entry **up_nde)
{
	const char *name = child->name;
	int namelen = child->len;
	unsigned long  n = i_size_read(dir);
	void *start, *end;
	struct nvmm_dir_entry *nde;
	struct nvmm_dir_entry *prev;

	start = NVMM_I(dir)->i_virt_addr;
	
	if(unlikely(n <= 0))
		goto out;

	nde = (struct nvmm_dir_entry *)start;
	prev = (struct nvmm_dir_entry *)start;
	end = start + n;

	while((void *)nde < end)
	{
		if(unlikely(nde->rec_len == 0)){
			nvmm_error(dir->i_sb,__FUNCTION__,"zero-length directory entry\n");
			goto out;
		}
		if(nvmm_match(namelen, name, nde))
			goto found;
		prev = (struct nvmm_dir_entry *)nde;
		nde = nvmm_next_entry(nde);
	}
out:
	
	return NULL;

found:

    *up_nde = prev;
	return nde;
}

ino_t nvmm_inode_by_name(struct inode *dir,struct qstr *child)
{
    ino_t res = 0;
    struct nvmm_dir_entry *de;
    struct nvmm_dir_entry *nouse = NULL;

	nvmm_establish_mapping(dir);
    de = nvmm_find_entry2(dir,child,&nouse);
    if(de)
        res = le32_to_cpu(de->inode);
	nvmm_destroy_mapping(dir);
	
    return res;
}

/*
  set the entry's file_type
 */
static inline void nvmm_set_de_type(struct nvmm_dir_entry *de,struct inode *inode)
{
	umode_t mode = inode->i_mode;
	de->file_type = nvmm_type_by_mode[(mode & S_IFMT) >> S_SHIFT];
}

/*
  set the 'inode' to the dentry 'de',this is a interface for rename function
 */
void nvmm_set_link(struct inode *dir, struct nvmm_dir_entry *de, struct inode *inode,
                   int update_times)
{
    struct page *page;
    page = nvmm_virt_to_page((char *)de);
    lock_page(page);
    
    de->inode = cpu_to_le64(inode->i_ino);
    nvmm_set_de_type(de, inode);
    if(likely(update_times))
        dir->i_mtime = dir->i_ctime = CURRENT_TIME_SEC;
    NVMM_I(dir)->i_flags &= ~NVMM_BTREE_FL;

    unlock_page(page);
    mark_inode_dirty(dir);
}

/*
  add the dentry of 'inode' to the dir
 */
int nvmm_add_link(struct dentry *dentry,struct inode *inode)
{
	struct inode *dir = dentry->d_parent->d_inode;
	const char *name = dentry->d_name.name;
    	int namelen = dentry->d_name.len;
    	unsigned reclen = NVMM_DIR_REC_LEN(namelen);
    	unsigned short rec_len = 0,name_len = 0;
    	unsigned long size;
        unsigned long nrpages;
        struct nvmm_inode_info *ni;
    	struct nvmm_dir_entry *de;
    	char *vaddr,*eaddr;
        char *dir_end;
        struct page *page;
    	int err = 0;
        int i;
    
    	ni = NVMM_I(dir);
    	vaddr = ni->i_virt_addr;
    	if(unlikely(!vaddr)){
        	err = -ENOMEM;
        	goto out;
    	}
        de = (struct nvmm_dir_entry *)vaddr;        
    	size = le64_to_cpu(dir->i_size);
        eaddr = vaddr + size;
        nrpages = size >> PAGE_SHIFT;

        for(i = 0;i <= nrpages;i++){
            
            page = nvmm_virt_to_page((char *)de);
            lock_page(page);
            dir_end = (void *)(de) + PAGE_SIZE - reclen;
    	
            while((char *)de <= dir_end){
                if((char *)de == eaddr){
                    name_len = 0;
            		rec_len = PAGE_SIZE;
            		goto need_new_block;
                }
                if(unlikely(de->rec_len == 0)){
                    nvmm_error(dir->i_sb,__func__,
                               "zero_length directory entry");
                    err = -EIO;
                    goto out;
                }
                if(nvmm_match(namelen,name,de)){
                    err = -EEXIST;
            		goto out;
                }
                name_len = NVMM_DIR_REC_LEN(de->name_len);//the dentry's real rec_len
                rec_len = le16_to_cpu(de->rec_len);
                if(!de->inode && rec_len >= reclen)   //case 2 : inode = 0 & space is enough
            		goto got_it;
                if(rec_len >= name_len + reclen)   //case 3 : real rec_len < de->rec_len   
            		goto got_it;
                de = (struct nvmm_dir_entry *)((char *)de + rec_len);
            }
            unlock_page(page);
        }
	
        return -EINVAL;
	
need_new_block:

	if(dir->i_size + PAGE_SIZE <= MAX_DIR_SIZE){
		err = nvmm_alloc_blocks(dir, 1);
		i_size_write(dir, dir->i_size + PAGE_SIZE);
		if(unlikely(err)){
			nvmm_error(dir->i_sb, __FUNCTION__, "alloc new block failed!\n");
			return err;
		}
	}else{
		err = -EIO;
		goto out;
	}

	de->rec_len = cpu_to_le16(PAGE_SIZE);
        de->inode = 0;

got_it:
	

	if(de->inode){
	struct nvmm_dir_entry *de1 = (struct nvmm_dir_entry *)((char *)de + name_len);
	int temp_rec_len = rec_len - name_len;
        de1->rec_len = cpu_to_le16(temp_rec_len);
	
        de->rec_len = cpu_to_le16(name_len);
	
        de = de1;
    	}
    	de->name_len = namelen;
    	memcpy(de->name,name,namelen);
    	de->inode = cpu_to_le64(inode->i_ino);
    	nvmm_set_de_type(de,inode);
    	dir->i_mtime = dir->i_ctime = CURRENT_TIME_SEC;
    	NVMM_I(dir)->i_flags &= ~NVMM_BTREE_FL;
        unlock_page(page);
    	mark_inode_dirty(dir);
    	err = 0;
out:
	return err;
}

/*
  delete the dentry, change the previous dentry's rec_len
 */
int nvmm_delete_entry(struct nvmm_dir_entry *dir,struct nvmm_dir_entry **pdir,
                      struct inode *parent)
{
    unsigned short rec_len = 0;
    unsigned short temp;
    struct nvmm_dir_entry *prev;
    struct page *page;
    int err = 0;
    prev = *pdir;
    page = nvmm_virt_to_page((char *)dir);
    
    if(unlikely(dir->rec_len == 0)){
        nvmm_error(parent->i_sb,__func__,
                   "zero_length directory entry");
        err = -EIO;
        goto out;
    }
    lock_page(page);
    if(likely(dir))
        rec_len = le16_to_cpu(dir->rec_len);
    dir->inode = 0;
    temp = le16_to_cpu(prev->rec_len);
    temp += rec_len;
    prev->rec_len = cpu_to_le16(temp);
    parent->i_ctime = parent->i_mtime = CURRENT_TIME_SEC;
    NVMM_I(parent)->i_flags &= ~NVMM_BTREE_FL;

    unlock_page(page);
    mark_inode_dirty(parent);

 out:
    return err;    
}

/*
  set '.'(the file itself) and '..'(parent) of the dentry 
 */
int nvmm_make_empty(struct inode *inode,struct inode *parent)
{
	struct nvmm_inode_info *ni;
	struct nvmm_dir_entry *de;
	void *vaddr;
	int err = 0;
    
	/*establish mapping*/
	err = nvmm_establish_mapping(inode);
	if(unlikely(err)){
		nvmm_error(inode->i_sb, __FUNCTION__, "establish mapping!\n");
		return err;
	}
	if(inode->i_size == 0){
		err = nvmm_alloc_blocks(inode, 1);
		i_size_write(inode, PAGE_SIZE);
		if(unlikely(err)){
			nvmm_error(inode->i_sb, __FUNCTION__, "alloc new block failed!\n");
			return err;
		}
	}else{
		err = -EIO;
		goto fail;
	}

	ni = NVMM_I(inode);
	vaddr = ni->i_virt_addr;
	if(unlikely(!vaddr)){
        	err = -ENOMEM;
       		goto fail;
    }

	de = (struct nvmm_dir_entry *)vaddr;
	de->name_len = 1;
	de->rec_len = cpu_to_le16(NVMM_DIR_REC_LEN(1));
	memcpy(de->name,".\0\0",4);
	de->inode = cpu_to_le32(inode->i_ino);
	nvmm_set_de_type(de,inode);

    	de = (struct nvmm_dir_entry *)(vaddr + NVMM_DIR_REC_LEN(1));
	de->name_len = 2;
	de->rec_len = cpu_to_le16(PAGE_SIZE - NVMM_DIR_REC_LEN(1));
    	de->inode = cpu_to_le32(parent->i_ino);
   	memcpy(de->name,"..\0",4);
   	nvmm_set_de_type(de,inode);
fail:
	/*destroy table mapping*/
	err = nvmm_destroy_mapping(inode); //maybe wrong ,don't assign the err
	return err;
}

/*nvmm_dotdot: find the dentry of the given inode(dir)'s parent,
               that is, '..'
  input : inode
  output : dentry
*/
struct nvmm_dir_entry *nvmm_dotdot(struct inode *dir)
{
    struct nvmm_dir_entry *de = NULL;
    struct nvmm_inode_info *ni = NVMM_I(dir);
    void *vaddr = NULL;
    
    vaddr = ni->i_virt_addr;
    de = nvmm_next_entry((struct nvmm_dir_entry *)vaddr);
    return de;
}

/*
  check the dir is empty or not 
 */
int nvmm_empty_dir(struct inode *inode)
{
	struct nvmm_inode_info *ni;
    struct nvmm_dir_entry *de;
    unsigned long size = le64_to_cpu(inode->i_size);
    void *vaddr;

    ni = NVMM_I(inode);
    vaddr = ni->i_virt_addr;
    de = (struct nvmm_dir_entry *)vaddr;
    vaddr += size - NVMM_DIR_REC_LEN(1);

    while((void *)de <= vaddr){
        if(unlikely(de->rec_len == 0)){
            nvmm_error(inode->i_sb,__func__,
                       "zero_length directory entry");
            printk("vaddr=%p,de=%p\n",vaddr,de);
            goto not_empty;
        }
        if(de->inode != 0){
            if(de->name[0] != '.')
                goto not_empty;
            if(de->name_len > 2)
                goto not_empty;
            if(de->name_len < 2){
                if(de->inode != cpu_to_le32(inode->i_ino))
                    goto not_empty;
            }else if(de->name[1] != '.')
                goto not_empty;
        }
        de = nvmm_next_entry(de);
    }
    return 1;
 not_empty:
    return 0;
}

const struct file_operations nvmm_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate	= nvmm_readdir,
	.unlocked_ioctl	= nvmm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= nvmm_compat_ioctl,
#endif
	.fsync		= noop_fsync,
};




