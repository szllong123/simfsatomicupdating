/*
 * linux/fs/simfs/pgtable.c
 * 
 * Copyright (C) 2013 College of Computer Science,
 * Chonqing University
 *
 * author: K.Zhong<kzhong1991@gmail.com>
 *
 * File page table management. 
 *
 */

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/mm_types.h>
#include <linux/kernel.h>
#include "nvmm.h"
#include "nvmalloc.h"

#ifndef page_to_phys
#define page_to_phys(page)      ((dma_addr_t)page_to_pfn(page) << PAGE_SHIFT)
#endif

inline void nvmm_setup_pte(pte_t *pte, struct page *pg)
{
   set_pte(pte, mk_pte(pg, PAGE_KERNEL));
}

inline void nvmm_setup_pmd(pmd_t *pmd, pte_t *pte)
{
    set_pmd(pmd, __pmd(__pa(pte) | _PAGE_TABLE));
}

inline void nvmm_setup_pud(pud_t *pud, pmd_t *pmd)
{
    set_pud(pud, __pud(__pa(pmd) | _PAGE_TABLE));
}


inline pud_t* nvmm_get_pud(struct super_block *sb, u64 ino)
{
    struct nvmm_inode *ni = nvmm_get_inode(sb, ino);
    return (pud_t*)__va(ni->i_pg_addr);
}

inline pmd_t* nvmm_get_pmd(pud_t *pud)
{
    return (pmd_t*)__va(pud_val(*pud) & PAGE_MASK);
}

inline pte_t* nvmm_get_pte(pmd_t *pmd)
{
    return (pte_t*)__va(pmd_val(*pmd) & PAGE_MASK);
}

inline void nvmm_rm_pud(pud_t *pud)
{
    pud_clear(pud);
}

inline void nvmm_rm_pmd(pmd_t *pmd)
{
    pmd_clear(pmd);
}

inline void nvmm_rm_pte(pte_t *pte)
{
   memset((void*)pte, 0x00, PAGE_SIZE);
    //pte_clear(pte);
}



inline pud_t *nvmm_pud_alloc_one(struct super_block *sb)
{
    return (pud_t*)nvmm_get_zeroed_page(sb);
}

void nvmm_pud_free(struct super_block *sb, pud_t *pud)
{
    unsigned long pagefn = __pa((unsigned long)pud) >> PAGE_SHIFT;
    nvmm_free_block(sb, pagefn);
}

inline pmd_t *nvmm_pmd_alloc_one(struct super_block *sb)
{
    return (pmd_t*)nvmm_get_zeroed_page(sb);
}

void nvmm_pmd_free(struct super_block *sb, pmd_t *pmd)
{
    unsigned long pagefn = __pa((unsigned long)pmd) >> PAGE_SHIFT;
    nvmm_free_block(sb, pagefn);
}

inline pte_t *nvmm_pte_alloc_one(struct super_block *sb)
{
    return (pte_t*)nvmm_get_zeroed_page(sb);
}

void nvmm_pte_free(struct super_block *sb, pte_t *pte)
{
    unsigned long pagefn = __pa((unsigned long)pte) >> PAGE_SHIFT;
    nvmm_free_block(sb, pagefn);
}


int __nvmm_pmd_alloc(struct super_block *sb, pud_t *pud)
{
    pmd_t *new = nvmm_pmd_alloc_one(sb);
    if (!new)
        return -ENOMEM;
    
    smp_wmb();

    if(pud_present(*pud)) /* Another has populated it */
        nvmm_pmd_free(sb, new);
    else
        nvmm_setup_pud(pud, new);

    return 0;
}

int __nvmm_pte_alloc(struct super_block *sb, pmd_t *pmd)
{
    pte_t *new = nvmm_pte_alloc_one(sb);
    if (!new)
        return -ENOMEM;

    smp_wmb();

    if(pmd_present(*pmd))
        nvmm_pte_free(sb, new);
    else
        nvmm_setup_pmd(pmd, new);

    return 0;
}


pud_t *nvmm_pud_alloc(struct super_block *sb, unsigned long ino, unsigned long offset)
{
    int i = 0;
    int nr = offset >> PUD_SHIFT;
    pud_t *pud = nvmm_get_pud(sb, ino);

    //printk(KERN_INFO "nvmm_pud_alloc: nr = %d\n", nr);

    while(i++ < nr) pud++;

    return pud;
}


pmd_t *nvmm_pmd_alloc(struct super_block *sb, pud_t *pud, unsigned long addr)
{
    if (addr >= NVMM_START && 
		    addr + MAX_DIR_SIZE - 1 < NVMM_START + DIR_AREA_SIZE )
    {
    	return nvmm_get_pmd(pud);	    
    }
    else {
    	return (unlikely(pud_none(*pud)) && __nvmm_pmd_alloc(sb, pud))?
        	NULL: pmd_offset(pud, addr);
    }
}

pte_t *nvmm_pte_alloc(struct super_block *sb, pmd_t *pmd, unsigned long addr)
{
    return (unlikely(pmd_none(*pmd)) && __nvmm_pte_alloc(sb, pmd))?
        NULL: pte_offset_kernel(pmd, addr);
}





/*
 * Initialize the file page table.
 * When an inode is requested, set up the page table for it.
 */
int nvmm_init_pg_table(struct super_block * sb, u64 ino)
{
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    struct nvmm_inode *ni = nvmm_get_inode(sb, ino);

    pud = nvmm_pud_alloc_one(sb);
    pmd = nvmm_pmd_alloc_one(sb);
    pte = nvmm_pte_alloc_one(sb);

    if (pud == NULL || pmd == NULL || pte == NULL) {
        nvmm_error(sb, __FUNCTION__, "init file page table failed!\n");
        return -PGFAILD;
    }

    nvmm_setup_pmd(pmd, pte);
    nvmm_setup_pud(pud, pmd);

    ni->i_pg_addr = __pa(pud);

    return 0;
}


/*int nvmm_insert_pages(struct super_block *sb, struct inode *vfs_inode, 
            unsigned long num, phys_addr_t phys)
{
    unsigned long i = 0;
    struct nvmm_sb_info *nsi;
	struct nvmm_inode *ni;
    phys_addr_t base, next, cur;
    unsigned long *p;
    struct page *pg;
    int res = 0;

    nsi = NVMM_SB(sb);
    base = nsi->phy_addr;
    cur = phys;

    if (num == 0 || phys == 0)
        return -1;

    for (i = 0; i < num; i++)
    {
    	printk(KERN_INFO "MY_DEBUG: pfn = %lx\n", cur);
        p = (unsigned long*)__va(cur );
        next = base + *p;
		memset((void *)p,0,PAGE_SIZE);
        pg = pfn_to_page(cur >> PAGE_SHIFT);
	vfs_inode->i_blocks++;
	ni = nvmm_get_inode(vfs_inode->i_sb, vfs_inode->i_ino);
	ni->i_blocks =  cpu_to_le32(vfs_inode->i_blocks);

        res = nvmm_insert_page(sb, vfs_inode, pg);
        
        if (unlikely(res != 0))
            return res;        
        cur = next;
	printk(KERN_INFO"Insert ok\n");
    }
	//printk(KERN_INFO "In insert_pages, num = %lu\n", num);
	return 0;
}*/



/*
 * Insert one page to file page table and update the kernel page tablle.
 * Now it suport 32GB file.
 */

int nvmm_insert_page(struct super_block *sb, struct inode *vfs_inode, struct page *pg)
{
    unsigned long ino = 0;
    unsigned long addr = 0;
    unsigned long offset = 0;
    unsigned long new_addr = 0;
    unsigned long blocks = 0;

    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte; 

    ino = vfs_inode->i_ino;
    addr = (unsigned long)(NVMM_I(vfs_inode))->i_virt_addr;
    blocks = vfs_inode->i_blocks;
    offset = (blocks - 1) << PAGE_SHIFT;
    new_addr = addr + offset;


    pud = nvmm_pud_alloc(sb, ino, offset);
    if (unlikely(!pud)){
	    printk(KERN_INFO "nvmm_empty pud!!!\n");
   	    return -1;
    }

    if(unlikely(pud_none(*pud))) {  /* insert to kernel page table */
	    pmd = nvmm_pmd_alloc(sb, pud, new_addr);	
    	nvmap_pmd(new_addr, nvmm_get_pmd(pud), current->mm);
    }else
	    pmd = nvmm_pmd_alloc(sb, pud, new_addr);

    if (unlikely(!pmd)) {
	    printk(KERN_INFO "empty pmd!!!\n");
        return -1;
    }
    pte = nvmm_pte_alloc(sb, pmd, new_addr);
    if (unlikely(!pte)) {
	    printk(KERN_INFO "empty pte!!!\n");
	return -1;	
    }


    /* setup the pte entry for the new page */
    nvmm_setup_pte(pte, pg);

    return 0;
}


void nvmm_rm_pte_range(struct super_block *sb, pmd_t *pmd)
{
    pte_t *pte, *p;
    int cnt = 0;
    unsigned long pagefn;

    p = pte = nvmm_get_pte(pmd);
	
	if (!pte_none(*pte)){
		do {
			pagefn = (pte_val(*pte) & 0x0fffffffffffffff) >> PAGE_SHIFT;
			nvmm_free_block(sb, pagefn);
			pte++;
			cnt++;
		}while(!pte_none(*pte) && cnt < PTRS_PER_PTE);
	}
    nvmm_pte_free(sb, p);
}


void nvmm_rm_pmd_range(struct super_block *sb, pud_t *pud)
{
    pmd_t *pmd, *p;
    int cnt = 0;

    p = pmd = nvmm_get_pmd(pud);

    if (!pmd_none(*pmd)){
		do {
			nvmm_rm_pte_range(sb, pmd);
			pmd++;
			cnt++;
		}while(!pmd_none(*pmd) && cnt < PTRS_PER_PMD);
	}
    nvmm_pmd_free(sb, p);
}



void nvmm_rm_pg_table(struct super_block *sb, u64 ino)
{
    pud_t *pud, *p;
    struct nvmm_inode *ni;
    int cnt = 0;

    ni = nvmm_get_inode(sb, ino);
    p = pud = nvmm_get_pud(sb, ino);
    if (!pud_none(*pud)){
	    do {
		    nvmm_rm_pmd_range(sb, pud);
			pud++;
			cnt++;
		}while(!pud_none(*pud) && cnt < PTRS_PER_PUD);
	}
    nvmm_pud_free(sb, p);

    ni->i_pg_addr = 0;
}


/*
 * input :
 * @inode : vfs inode
 * returns :
 * 0 if success else others
 * establish mapping for the inode
 */
int nvmm_establish_mapping(struct inode *inode)
{
	struct nvmm_inode_info *ni_info;
	struct super_block *sb;
	int errval = 0;
	pud_t *pud;
	int mode = 0;
	unsigned long ino;
	unsigned long vaddr;
	struct mm_struct *mm;

	ni_info = NVMM_I(inode);
	sb = inode->i_sb;
	ino = inode->i_ino;
	pud = nvmm_get_pud(sb, ino);
	//mm = current->mm;
	mm = &init_mm;
	vaddr = (unsigned long) ni_info->i_virt_addr;
	if(!vaddr){
		if((S_ISDIR(inode->i_mode)) || (S_ISLNK(inode->i_mode))) {
			mode = 1;
			ni_info->i_virt_addr = nvmalloc(mode);
		}else if(S_ISREG(inode->i_mode)){
			mode = 0;
			ni_info->i_virt_addr = nvmalloc(mode);
		}
	}else{
//		printk("this is a multiple process");
	}
	vaddr = (unsigned long) ni_info->i_virt_addr;

	if ((vaddr >= NVMM_START && vaddr + MAX_DIR_SIZE - 1 < NVMM_START + DIR_AREA_SIZE) || 
		(vaddr >= NVMM_START + DIR_AREA_SIZE && vaddr < NVMM_END)){

		errval = nvmap(vaddr, pud, mm);

    	}else{
        	printk(KERN_WARNING "nvmap: unknow addr\n");
//		printk(KERN_INFO ".......%lx......\n",vaddr);
        	return -1;
    	}
	
	if(S_ISREG(inode->i_mode)){	
		atomic_inc(&ni_info->i_p_counter);
//		printk("open, ino = %ld, process num = %d, vaddr = %lx\n", ino, (ni_info->i_p_counter).counter, vaddr);
	}

	return errval;
}

/*
 * input :
 * @inode : vfs inode
 * returns :
 * 0 if success else others
 * destroy mapping for the inode
 */
int nvmm_destroy_mapping(struct inode *inode)
{
	struct nvmm_inode_info *ni_info;
	struct super_block *sb;
	int errval = 0;
	pud_t *pud;
	unsigned long ino;
	unsigned long vaddr;

	ni_info = NVMM_I(inode);
	vaddr = (unsigned long)ni_info->i_virt_addr;
	sb = inode->i_sb;
	ino = inode->i_ino;
	pud = nvmm_get_pud(sb, ino);

	if(!vaddr)
		return 0;
	errval = unnvmap(vaddr, pud, &init_mm);
	nvfree(ni_info->i_virt_addr);
	ni_info->i_virt_addr = 0;

	return errval;
}
