/*
 * linux/fs/nvmm/symlink.c
 *
 * Copyright (C) 2013 College of Computer Science,
 * Chonqing University
 *
 *
 */

#include <linux/fs.h>
#include <linux/namei.h>
#include "nvmm.h"
#include "xattr.h"

int nvmm_page_symlink(struct inode *inode, const char *symname, int len)
{
    struct nvmm_inode_info *ni_info;
    char *vaddr;
    int err;
    
    err = nvmm_alloc_blocks(inode, 1);
    if(err)
        return err;
    ni_info = NVMM_I(inode);
    nvmm_establish_mapping(inode);

    vaddr = (char *)(ni_info->i_virt_addr);
    memcpy(vaddr, symname, len);
    vaddr[len] = '\0';

    nvmm_destroy_mapping(inode);
    return 0;
}

static int nvmm_readlink(struct dentry *dentry, char __user *buffer , int buflen)
{
    struct inode *inode = dentry->d_inode;
    struct nvmm_inode_info *ni;
    char *vaddr;
    int err = 0;

    nvmm_establish_mapping(inode);
        
    ni = NVMM_I(inode);
    vaddr = (char *)(ni->i_virt_addr);
    err = vfs_readlink(dentry, buffer, buflen, vaddr);

    nvmm_destroy_mapping(inode);
    return err;    
}

static void *nvmm_follow_link(struct dentry *dentry, struct nameidata *nd)
{
    struct inode *inode = dentry->d_inode;
    struct nvmm_inode_info *ni;
    char *vaddr = NULL;
    int err;
    
    nvmm_establish_mapping(inode);
        
    ni = NVMM_I(inode);
    vaddr = (char *)(ni->i_virt_addr);
    err = vfs_follow_link(nd,vaddr);
    
    //nvmm_destroy_mapping(inode);
    return ERR_PTR(err);
}

struct inode_operations nvmm_symlink_inode_operations = {
	.readlink	= nvmm_readlink,
	.follow_link	= nvmm_follow_link,
  //.put_link = page_put_link,
	.setattr	= nvmm_notify_change,
#ifdef CONFIG_NVMMFS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= nvmm_listxattr,
	.removexattr	= generic_removexattr,
#endif
};

