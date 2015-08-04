/*
 * linux/fs/nvmm/file.c
 *
 * Copyright (C) 2013 College of Computer Science,
 * Chonqing University
 *
 * inode operations
 *
 */

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/aio.h>
#include <linux/slab.h>
#include <linux/uio.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include "nvmm.h"
#include "acl.h"
#include "xip.h"
#include "xattr.h"

#define PUD_SIZE_1 (PUD_SIZE - 1)
#define PMD_SIZE_1 (PMD_SIZE  -1)
#define PAGE_SIZE_1 (PAGE_SIZE - 1)

/*
 * input :
 * @vaddr : start virtual address
 * @iov : io control
 * @base : 
 * @bytes : the size to be copied
 * returns :
 * 0 if success else the left size non copied
 */
static size_t __nvmm_iov_copy_from(char *vaddr, const struct iovec *iov, size_t base, size_t bytes)
{
	size_t copied = 0, left = 0;
	while(bytes){
		char __user *buf = iov->iov_base + base;
		int copy = min(bytes, iov->iov_len - base);

		base = 0;
		left = __copy_from_user(vaddr, buf, copy);
		copied += copy;
		bytes -= copy;
		vaddr += copy;
		iov++;

		if(unlikely(left))
			break;

	}
	return copied -left;
}

/*
 * input :
 * @vaddr : start virtual address
 * @iov : io control
 * @base : 
 * @bytes : the size to be copied
 * returns :
 * 0 if success else the left size non copied
 */

static size_t __nvmm_iov_copy_to(char *vaddr, const struct iovec *iov, size_t base, size_t bytes)
{
	size_t copied = 0, left = 0;
	while(bytes){
		char __user *buf = iov->iov_base + base;
		int copy = min(bytes, iov->iov_len - base);

		base = 0;
		left = __copy_to_user(buf, vaddr, copy);
		copied += copy;
		bytes -= copy;
		vaddr += copy;
		iov++;
		
		if(unlikely(left))
			break;
	}
	return copied - left;
}

/*
 * input :
 * @to : start virtual address
 * @i : io iterator
 * @bytes : the size to be copied
 * returns :
 * 0 if success else the left size non copied
 */


static size_t nvmm_iov_copy_from(void * to, struct iov_iter *i, size_t bytes)
{
	size_t copied;
	if(likely(i->nr_segs == 1)){
		int left;
		char __user *buf = i->iov->iov_base + i->iov_offset;
		left = __copy_from_user(to, buf, bytes);
		copied = bytes - left;
	}else{
		copied = __nvmm_iov_copy_from(to, i->iov, i->iov_offset, bytes);
	}
	return copied;
}

/*
 * input :
 * @from : start virtual address
 * @i : io iterator
 * @bytes : the size to be copied
 * returns :
 * 0 if success else the left size non copied
 */
static size_t nvmm_iov_copy_to(void *from, struct iov_iter *i, size_t bytes)
{
	size_t copied;
	if(likely(i->nr_segs == 1)){
		int left;
		char __user *buf = i->iov->iov_base + i->iov_offset;
		left = __copy_to_user(buf, from, bytes);
		copied = bytes - left;
	}else{
		copied = __nvmm_iov_copy_to(from, i->iov, i->iov_offset, bytes);
	}
	return copied;
}
/*
 * input :
 * @inode : vfs inode, the file to be open
 * @file : file descriptor
 * returns :
 * 0 if success else others
 */
static int nvmm_open_file(struct inode *inode, struct file *filp)
{
	int errval = 0;
//	pid_t pid = current->pid;
	errval = nvmm_establish_mapping(inode);
//	printk("the process pid is : %d\n", pid);
	if(errval){
		nvmm_error(inode->i_sb, __FUNCTION__, "can't establish mapping\n");
		return errval;
	}

	filp->f_flags |= O_DIRECT;
	return generic_file_open(inode, filp);
}


/*
 *
 */

static int nvmm_release_file(struct file * file)
{
    struct inode *inode = file->f_mapping->host;
	struct nvmm_inode_info *ni_info;
	unsigned long vaddr;
	int err = 0;
	ni_info = NVMM_I(inode);
	vaddr = (unsigned long)ni_info->i_virt_addr;
	if(vaddr){
		if(atomic_dec_and_test(&ni_info->i_p_counter))
			err = nvmm_destroy_mapping(inode);

//		printk("release, ino = %ld, process num = %d, vaddr = %lx\n", inode->i_ino, (ni_info->i_p_counter).counter, vaddr);

	}else{
//		nvmm_info("the viraddr has already been released, you must have some thing wrong!\n");
		err = -1;
	}
	return err;
}


/**
 * find atomic updating pointer level
 * for PTE_ENTRY level, we want to allocate 1 page,
 * for PMD_ENTRY level, we at most allocate 512 pages, to limite page num in this scope, we have to make page_num_mask value 0x3ff
 * for PUD_ENTRY and PGD_ENTRY level, we get the similar situation with PMD_ENTRY
 * @offset : writing start address
 * @length : writing length
 *
 * return : 1 PTE_ENTRY; 0x3ff PMD_ENTRY; 0x7ffff PUD_ENTRY; 0xfffffff  PGD_ENTRY else 0
*/

static unsigned long nvmm_find_atomic_pointer_level(loff_t offset, size_t length)
{
	size_t end_write_position = offset + length - 1;
	int start_write_num, end_write_num;
	unsigned long page_num_mask = 0;

	//PTE_ENTRY ?
	start_write_num = offset >> PAGE_SHIFT;
	end_write_num = end_write_position >> PAGE_SHIFT;
	if(start_write_num == end_write_num){
		page_num_mask = 1;
		return page_num_mask;
	}

	//PMD_ENTRY ?
	start_write_num = offset >> PMD_SHIFT;
	end_write_num = end_write_position >> PMD_SHIFT;
	if(start_write_num == end_write_num){
		page_num_mask = 0x1ff;
		return page_num_mask;
	}

	//PUD_ENTRY ?
	start_write_num = offset >> PUD_SHIFT;
	end_write_num = end_write_position >> PUD_SHIFT;
	if(start_write_num == end_write_num){
		page_num_mask = 0x3ffff;
		return page_num_mask;
	}

	//PGD_ENTRY
	start_write_num = offset >> PGDIR_SHIFT;
	end_write_num = end_write_position >> PGDIR_SHIFT;
	if(start_write_num == end_write_num){
		page_num_mask = 0x7ffffff;
		return page_num_mask;
	}

	return page_num_mask;
}

/**
 * allocate pages for temp file
 * @consistency_i : consistency file vfs inode
 * @offset : writing start address
 * @length : writing length
 * @size : origin file size
 * @page_num_mask : update level of origin file, this also reflect pages to allocate for temp file
 * return 0 if success else 1
*/

static int nvmm_alloc_consistency_file_pages(struct inode *consistency_i , loff_t offset, size_t length, size_t size, unsigned long page_num_mask)
{
	int retval = 0;
	int pages_to_alloc = 0;
	size_t end_length = offset + length;

	if(end_length >= size){
		pages_to_alloc = (end_length + PAGE_SIZE_1) >> PAGE_SHIFT;
	}else{
		pages_to_alloc = (size + PAGE_SIZE_1) >> PAGE_SHIFT;
	}
	
	pages_to_alloc &= page_num_mask;
	if(0 == pages_to_alloc)
		pages_to_alloc = (1 == page_num_mask) ? page_num_mask : (page_num_mask + 1);
	retval = nvmm_alloc_blocks(consistency_i, pages_to_alloc);
	return retval;
}

/**
 * fetch value from PTE_ENTRY and turn it into struct page
 * @pte_t : PTE_ENTRY
 * @offset : offset to origin file
*/

static inline struct page *nvmm_get_pte_entry(pte_t * const pte)
{
	return pfn_to_page(pte_pfn(*pte));
}

/**
 * find atomic updating pointer, switch it with the temp file
 * @sb : vfs super_block
 * @normal_i : normal file vfs inode
 * @consistency_i : consistency file vfs inode
 * @offset : start write relative position of origin file
 * @page_num_mask : update level of origin file, this also reflect pages to allocate for temp file
*/

static void nvmm_atomic_update_pointer(struct super_block *sb, struct inode *normal_i, struct inode *consistency_i, loff_t offset, unsigned long page_num_mask)
{
	pud_t *pud_normal, *pud_con;
	pmd_t *pmd_normal, *pmd_con;
	pte_t *pte_normal, *pte_con;
	struct page *pg_normal, *pg_con;

	pud_normal = nvmm_get_pud(sb, normal_i->i_ino);
	pud_normal += (offset >> PUD_SHIFT);
	pud_con = nvmm_get_pud(sb, consistency_i->i_ino);
	pmd_con = pmd_offset(pud_con, 0);
	pte_con = pte_offset_kernel(pmd_con, 0);
	pg_con = nvmm_get_pte_entry(pte_con);

	if(1 == page_num_mask){
		if(pud_none(*pud_normal)){
			nvmm_setup_pud(pud_normal, pmd_con);
			pud_clear(pud_con);
		}else{
			pmd_normal = pmd_offset(pud_normal, offset);
			if(pmd_none(*pmd_normal)){
				nvmm_setup_pmd(pmd_normal, pte_con);
				pmd_clear(pmd_con);
			}else{
				pte_normal = pte_offset_kernel(pmd_normal, offset);
				if(!pte_none(*pte_normal)){
					pg_normal = nvmm_get_pte_entry(pte_normal);
					nvmm_setup_pte(pte_con, pg_normal);
				}else{
					set_pte(pte_con, __pte(0));
				}
				nvmm_setup_pte(pte_normal, pg_con);
			}
		}
		
	}else if(0x1ff == page_num_mask){
		if(pud_none(*pud_normal)){
			nvmm_setup_pud(pud_normal, pmd_con);
			pud_clear(pud_con);
		}else{
			pmd_normal = pmd_offset(pud_normal, offset);
			if(!pmd_none(*pmd_normal)){
				pte_normal = pte_offset_kernel(pmd_normal, offset);
				nvmm_setup_pmd(pmd_con, pte_normal);
			}else{
				pmd_clear(pmd_con);
			}
			nvmm_setup_pmd(pmd_normal, pte_con);
		}
	}else if(0x3ffff == page_num_mask){
		if(!pud_none(*pud_normal)){
			pmd_normal = pmd_offset(pud_normal, offset);
			nvmm_setup_pud(pud_con, pmd_normal);
		}else{
			pud_clear(pud_con);
		}
		nvmm_setup_pud(pud_normal, pmd_con);
	}else{
		nvmm_error(sb, __FUNCTION__, "atomic update pointer error");
	}
}


/**
 *
 *
*/

static int nvmm_consistency_function(struct super_block *sb, struct inode *normal_i, loff_t offset, size_t length, struct iov_iter *iter)
{
	struct inode *consistency_i;
	struct nvmm_inode *con_nvmm_inode;
	struct nvmm_inode_info *normal_i_info, *consistency_i_info;
	unsigned long normal_vaddr, consistency_vaddr;
	unsigned long start_cp_addr, start_cp_length, end_cp_addr, end_cp_length;
	void *copy_start_normal_vaddr, *copy_end_normal_vaddr, *copy_start_con_vaddr, *copy_end_con_vaddr;
	void *con_write_start_vaddr;
	unsigned long page_num_mask = 0;
	int retval = 0;
	struct nvmm_sb_info *nsi = NVMM_SB(sb);
	struct inode *root_i = nvmm_iget(sb, NVMM_ROOT_INO);
	loff_t size = i_size_read(normal_i);
	consistency_i = nvmm_new_inode(root_i, 0, NULL);
	consistency_i->i_mode = cpu_to_le16(nsi->mode | S_IFREG);
	con_nvmm_inode = nvmm_get_inode(sb, consistency_i->i_ino);
	if(!con_nvmm_inode->i_pg_addr)
		nvmm_init_pg_table(sb, consistency_i->i_ino);
	retval = nvmm_establish_mapping(consistency_i);
	consistency_i_info = NVMM_I(consistency_i);
	normal_i_info = NVMM_I(normal_i);
	normal_vaddr = (unsigned long)normal_i_info->i_virt_addr;
	consistency_vaddr = (unsigned long)consistency_i_info->i_virt_addr;

	//1. find atomic pointer level and get page_num_mask
	page_num_mask = nvmm_find_atomic_pointer_level(offset, length);
	if(0 == page_num_mask){
		retval = -1;
		nvmm_error(sb, __FUNCTION__, "find atomic pointer for consistency file failed");
		goto out;
	}

	//2. allocate pages for consistency file
	retval = nvmm_alloc_consistency_file_pages(consistency_i, offset, length, size, page_num_mask);
	if(retval){
		nvmm_error(sb, __FUNCTION__, "allocate pages for consistency file failed");
		goto out;
	}

	//3. calculate copy data
	if(1 == page_num_mask){
		start_cp_addr = offset & PAGE_MASK;
		con_write_start_vaddr = (void *)(consistency_vaddr + (offset & PAGE_SIZE_1));
	}else if(0x1ff == page_num_mask){
		start_cp_addr = offset & PMD_MASK;
		con_write_start_vaddr = (void *)(consistency_vaddr + (offset & PMD_SIZE_1));
	}else if(0x3ffff == page_num_mask){
		start_cp_addr = offset & PUD_MASK;
		con_write_start_vaddr = (void *)(consistency_vaddr + (offset & PUD_SIZE_1));
	}else if(0x7ffffff == page_num_mask){
		start_cp_addr = offset & PGDIR_MASK;
		nvmm_iov_copy_from((void *)normal_vaddr + offset, iter, length);
		goto out;
	}else{
		nvmm_error(sb, __FUNCTION__, "find atomic pointer for consistency file error2");
	}

	//length = end_position - start_position + 1, but offset is the write position so do not need
	start_cp_length = offset - start_cp_addr;
	if(offset + length < size){
		end_cp_addr = offset + length;
		end_cp_length = size - end_cp_addr;
	}

	//4. copy data 
	copy_start_normal_vaddr = (void *)(normal_vaddr + start_cp_addr);
	copy_start_con_vaddr = (void *)(consistency_vaddr + start_cp_addr);
	copy_end_normal_vaddr = (void *)(normal_vaddr + end_cp_addr);
	copy_end_con_vaddr = (void *)(consistency_vaddr + end_cp_addr);
	if(start_cp_length > 0){
		memcpy(copy_start_con_vaddr, copy_start_normal_vaddr, start_cp_length);
	}
	if(offset + length < size){
		memcpy(copy_end_con_vaddr, copy_end_normal_vaddr, end_cp_length);
	}
	nvmm_iov_copy_from(con_write_start_vaddr, iter, length);
	//5. get atomic updating pointer and update it
	nvmm_atomic_update_pointer(sb, normal_i, consistency_i, offset, page_num_mask);	

	//6. delete temp file inode
	nvmm_destroy_mapping(consistency_i);
	consistency_i->__i_nlink = 0;
	consistency_i->i_state |= I_FREEING;
	nvmm_evict_inode(consistency_i);
	if(0x7ffffff == page_num_mask){
		pud_t *pud = nvmm_get_pud(sb, normal_i->i_ino);
		nvmap(normal_vaddr, pud, current->mm);
	}
		
out :
	return retval;
}

ssize_t nvmm_direct_IO(int rw, struct kiocb *iocb,
		   const struct iovec *iov,
		   loff_t offset, unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	struct super_block *sb = inode->i_sb;
	ssize_t retval = 0;
	int hole = 0;
	struct iov_iter iter;
	loff_t size;
	void *start_vaddr = NVMM_I(inode)->i_virt_addr + offset;
	size_t length = iov_length(iov, nr_segs);
//	unsigned long pages_exist = 0, pages_to_alloc = 0,pages_needed = 0;        

	if(rw == READ)
		rcu_read_lock();
	size = i_size_read(inode);

	if(length < 0){
		retval = -EINVAL;
		goto out;
	}

	if((rw == READ)&&(offset + length > size))
		length = size - offset;

	if(!length)
		goto out;
    
	iov_iter_init(&iter, iov, nr_segs, length, 0);
	if(rw == READ){
		if(unlikely(hole)){
			
		}else{
			retval = nvmm_iov_copy_to(start_vaddr, &iter, length);
			if(retval != length){
				retval = -EFAULT;
				goto out;
			}
		}		
	}else if(rw == WRITE) {
/**        pages_needed = ((offset + length + sb->s_blocksize - 1) >> sb->s_blocksize_bits);
        pages_exist = (size + sb->s_blocksize - 1) >> sb->s_blocksize_bits;
        pages_to_alloc = pages_needed - pages_exist;

		if(pages_to_alloc > 0){
		
			retval = nvmm_alloc_blocks(inode, pages_to_alloc);
	
			if (retval){
				nvmm_info("alloc blocks failed!\n");
				goto out;
			}
		}
*/
		nvmm_alloc_blocks(inode, 0);
		nvmm_consistency_function(sb, inode, offset, length, &iter);
		retval = length;
/*		retval = nvmm_iov_copy_from(start_vaddr, &iter, length);
		if(retval != length){
			retval = -EFAULT;
			goto out;
		}
*/
	}

out :
	if(rw == READ)
		rcu_read_unlock();
	return retval;
}

/*
 * input :
 * @flags : 
 * returns :
 * 0 if use direct IO ways, else others.
 */
static int nvmm_check_flags(int flags)
{
	if(!(flags&O_DIRECT))
		return -EINVAL;

	return 0;
}
const struct file_operations nvmm_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= generic_file_aio_read,
	.aio_write	= generic_file_aio_write,
	.mmap		= generic_file_mmap,
	.open		= nvmm_open_file,
	.nvrelease	= nvmm_release_file,	
	.fsync		= noop_fsync,
	.check_flags	= nvmm_check_flags,
};



#ifdef CONFIG_NVMM_XIP
const struct file_operations nvmm_xip_file_operations = {
/*	.llseek		= generic_file_llseek,
	.read		= xip_file_read,
	.write		= xip_file_write,
	.mmap		= xip_file_mmap,
	.open		= generic_file_open,
	.fsync		= noop_fsync,*/
};
#endif


const struct inode_operations nvmm_file_inode_operations = {
#ifdef CONFIG_NVMMFS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= nvmm_listxattr,
	.removexattr	= generic_removexattr,
#endif
	.setattr	= nvmm_notify_change,
	.get_acl	= nvmm_get_acl,
};

