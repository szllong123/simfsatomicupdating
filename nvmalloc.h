/*
 * linux/include/linux/nvmalloc.h
 *
 * Copyright (C) 2013 College of Computer Science,
 * Chonqing University
 * 
 * Non-Volatile Main Memory File System 
 * A new filesystem designed by 
 * Chongqing University, College of Computer Science
 *
 * file virtual address management
 *
 */

#ifndef _LINUX_NVMALLOC_H
#define _LINUX_NVMALLOC_H

#include <linux/init.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <asm/page.h>

#ifndef NVMM_START
#define NVMM_START       (0xffffcb0000000000)
#endif

#ifndef NVMM_END
#define NVMM_END         (0xffffe8ffffffffff)
#endif
#define DIR_AREA_SIZE    (1UL << 35) // 32G

#define PMD_BYTE_SIZE    (1UL << 30) // 1G

struct nvm_struct {
    struct nvm_struct   *next;
    void                *vaddr;
    unsigned long       size;
};

/*
extern void *nvmalloc(const int mode);
extern void nvfree(const void *addr);
extern int nvmap(unsigned long addr, pud_t *ppud, struct mm_struct *mm);
extern int nvmap_pmd(const unsigned long addr, pmd_t *pmd, struct mm_struct *mm);
extern int unnvmap(unsigned long addr, pud_t *ppud, struct mm_struct *mm);
extern int unnvmap_pmd(const unsigned long addr, pmd_t *pmd, struct mm_struct *mm);
extern void print_free_list(int mode);
extern void print_used_list(int mode);
*/
#endif // _LINUX_NVMALLOC_H

