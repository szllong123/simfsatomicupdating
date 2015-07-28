/*
 * linux/fs/nvmm/namei.c
 *
 * Copyright (C) 2013 College of Computer Science,
 * Chonqing University
 *
 * inode operations
 *
 */
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include "nvmm.h"
#include "acl.h"
#include "xattr.h"
#include "xip.h"


/*
 * input :
 * @dentry : vfs dentry
 * @inode : vfs inode
 * returns :
 * 0 if success else others
 * add inode to his parent
 */
static inline int nvmm_add_nondir(struct dentry *dentry, struct inode *inode)
{
	int err = 0;

	err = nvmm_add_link(dentry, inode);
	if(!err){
		unlock_new_inode(inode);
		d_instantiate(dentry, inode);
		return 0;
	}
	inode_dec_link_count(inode);
	unlock_new_inode(inode);
	iput(inode);
	return err;
}

static struct dentry *nvmm_lookup(struct inode *dir,struct dentry *dentry,unsigned int flags)
{
    struct inode *inode;
    ino_t ino;

    if(unlikely(dentry->d_name.len > NVMM_NAME_LEN))
        return ERR_PTR(-ENAMETOOLONG);

    ino = nvmm_inode_by_name(dir,&dentry->d_name);
    inode = NULL;
    if(likely(ino)){
        inode = nvmm_iget(dir->i_sb,ino);
        if(unlikely(inode == ERR_PTR(-ESTALE))){
            nvmm_error(dir->i_sb,__func__,
                       "deleted inode referenced:%u",
                       (unsigned long)ino);
            return ERR_PTR(-EIO);
        }
    }
    return d_splice_alias(inode,dentry);
}

/*
 * input :
 * @dir : vfs inode ,parent inode 
 * @dentry : vfs dentry ,dentry for to be created inode
 * @mode : the mode for to be created inode
 * @ excl : pass from vfs ,non using in this function
 * returns :
 * 0 is success else others
 */
static int nvmm_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	struct inode *inode;
	int err = 0;
	dquot_initialize(dir);

	inode = nvmm_new_inode(dir, mode, &dentry->d_name);
	if(IS_ERR(inode))
		return PTR_ERR(inode);

	inode->i_op = &nvmm_file_inode_operations;
	if(nvmm_use_xip(inode->i_sb)){
		inode->i_mapping->a_ops =  &nvmm_aops_xip;
		inode->i_fop = &nvmm_xip_file_operations;
	}else{
		inode->i_mapping->a_ops = &nvmm_aops;
		inode->i_fop = &nvmm_file_operations;
	}
	mark_inode_dirty(inode);
	err = nvmm_establish_mapping(dir);
	err = nvmm_add_nondir(dentry, inode);
	err = nvmm_destroy_mapping(dir);
	return err;
}

/*
 * input :
 * @dir : vfs inode, directory inode
 * @dentry : info about inode
 * @mode : type and authority about inode to be created
 * @returns :
 * 0 if success else others
 */
static int nvmm_tmpfile(struct inode *dir, struct dentry *dentry, umode_t mode)
{ 
	struct inode *inode = nvmm_new_inode(dir, mode, NULL);
	if(IS_ERR(inode))
		return PTR_ERR(inode);

	inode->i_op = &nvmm_file_inode_operations;
	if(nvmm_use_xip(inode->i_sb)){
		inode->i_mapping->a_ops = &nvmm_aops_xip;
		inode->i_fop = &nvmm_xip_file_operations;
	}else{
		inode->i_mapping->a_ops = &nvmm_aops;
		inode->i_fop = &nvmm_file_operations;
	}
	d_tmpfile(dentry, inode);
	unlock_new_inode(inode);
	return 0;
}

/*
 * input :
 * @dir : vfs inode ,parent inode 
 * @dentry : vfs dentry ,dentry for to be created inode
 * @mode : the mode for to be created inode
 * @ rdev : pass from vfs, Verification if should be created
 * returns :
 * 0 is success else others
 */
static int nvmm_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct inode *inode;
	int err;
	if(!new_valid_dev(rdev))
		return -EINVAL;

	dquot_initialize(dir);

	inode = nvmm_new_inode(dir, mode, &dentry->d_name);
	err = PTR_ERR(inode);
	if(!IS_ERR(inode)){
		mark_inode_dirty(inode);
		err = nvmm_add_nondir(dentry, inode);	
	}
	return err;

}

/*
  create the symbolic link
 */
static int nvmm_symlink(struct inode *dir, struct dentry *dentry,
                        const char *symname)
{
    struct super_block *sb = dir->i_sb;
    int err = -ENAMETOOLONG;
    unsigned l = strlen(symname);
    struct inode *inode;

    if(unlikely(l+1 > sb->s_blocksize))
        goto out;

    dquot_initialize(dir);

    inode = nvmm_new_inode(dir, S_IFLNK | S_IRWXUGO , &dentry->d_name);
    err = PTR_ERR(inode);
    if(unlikely(IS_ERR(inode)))
        goto out;

    inode->i_op = &nvmm_symlink_inode_operations;
    inode->i_mapping->a_ops = &nvmm_aops;

    err = nvmm_page_symlink(inode, symname, l);
    if(unlikely(err))
        goto out_fail;

    inode->i_size = l;
    nvmm_write_inode(inode, 0);

    nvmm_establish_mapping(dir);
    
    err = nvmm_add_nondir(dentry, inode);

    nvmm_destroy_mapping(dir);
    
 out:
    return err;

 out_fail:
    inode_dec_link_count(inode);
    unlock_new_inode(inode);
    iput(inode);
    goto out;
}

/*
  create the hard link
*/
static int nvmm_link(struct dentry * old_entry, struct inode *dir,
                     struct dentry *dentry)
{
    struct inode *inode = old_entry->d_inode;
    int err;

    dquot_initialize(dir);
    inode->i_ctime = CURRENT_TIME_SEC;
    inode_inc_link_count(inode);
    ihold(inode);

    nvmm_establish_mapping(dir);
    
    err = nvmm_add_link(dentry, inode);
    if(!err){
        d_instantiate(dentry, inode);
        nvmm_destroy_mapping(dir);
        return 0;
    }
    inode_dec_link_count(inode);
    iput(inode);
    nvmm_destroy_mapping(dir);
    return err;
}

static int nvmm_unlink(struct inode *dir, struct dentry *dentry)
{
    struct inode *inode = dentry->d_inode;
    struct nvmm_dir_entry *de;
    struct nvmm_dir_entry *pde;
	int err = -ENOENT;

    dquot_initialize(dir);
    
    nvmm_establish_mapping(dir);
    de = nvmm_find_entry2(dir,&dentry->d_name,&pde);
    if(unlikely(!de))
        goto out;

    err = nvmm_delete_entry(de,&pde,dir);
    if(unlikely(err))
        goto out;

    inode->i_ctime = dir->i_ctime;
    inode_dec_link_count(inode);
    err = 0;
 out:
    nvmm_destroy_mapping(dir);
	return err;
}

static int nvmm_mkdir(struct inode *dir,struct dentry *dentry,umode_t mode)
{
    struct inode *inode;
    int err;

    dquot_initialize(dir);
    
    inode_inc_link_count(dir);

    inode = nvmm_new_inode(dir,S_IFDIR | mode,&dentry->d_name);
    err = PTR_ERR(inode);
    if(unlikely(IS_ERR(inode)))
        goto out_dir;

    inode->i_op = &nvmm_dir_inode_operations;
    inode->i_fop = &nvmm_dir_operations;
    inode->i_mapping->a_ops = &nvmm_aops;

    inode_inc_link_count(inode);

    nvmm_establish_mapping(dir);

    err = nvmm_make_empty(inode,dir);//set the first fragment of directory
    if(unlikely(err))
        goto out_fail;
    err = nvmm_add_link(dentry,inode);
    if(unlikely(err))
        goto out_fail;

    unlock_new_inode(inode);
    d_instantiate(dentry,inode);//indicate that it is now in use by the dcache
 out:

    nvmm_destroy_mapping(dir);
    return err;

 out_fail:
    inode_dec_link_count(inode);
    inode_dec_link_count(inode);
    unlock_new_inode(inode);
    iput(inode);
 out_dir:
    inode_dec_link_count(dir);
    goto out;
}

static int nvmm_rmdir(struct inode *dir,struct dentry *dentry)
{
    struct inode *inode = dentry->d_inode;
    int err = -ENOTEMPTY;

    nvmm_establish_mapping(inode);
    
    if(nvmm_empty_dir(inode)){
        err = nvmm_unlink(dir,dentry);  //remove the dentry from dir
        if(!err){
            inode->i_size = 0;
            inode_dec_link_count(inode);
            inode_dec_link_count(dir);
        }
    }
    
    nvmm_destroy_mapping(inode);
    return err;
}

static int nvmm_rename(struct inode *old_dir, struct dentry *old_dentry,
                       struct inode *new_dir, struct dentry *new_dentry)
{
    struct inode *old_inode = old_dentry->d_inode;
    struct inode *new_inode = new_dentry->d_inode;
    struct nvmm_dir_entry *dir_de = NULL;
    struct nvmm_dir_entry *old_de;
    struct nvmm_dir_entry *old_pde = NULL;
	struct nvmm_dir_entry *new_pde = NULL;
    int err = -ENOENT;
	int exist = 0;
	int isdir = 0;

    nvmm_establish_mapping(old_dir);
    old_de = nvmm_find_entry2(old_dir, &old_dentry->d_name, &old_pde);
    if(!old_de)
        goto out_old_dir;

    if(S_ISDIR(old_inode->i_mode)){
		isdir = 1;
        err = -EIO;
        nvmm_establish_mapping(old_inode);
        dir_de = nvmm_dotdot(old_inode);
        if(!dir_de)
            goto out_old_inode;
    }

    if(new_inode){
        struct nvmm_dir_entry *new_de;
        exist = 1;
		
        err = -ENOTEMPTY;
        nvmm_establish_mapping(new_inode);
        if(dir_de && !nvmm_empty_dir(new_inode))// old_inode is dir & new_inode is not empty
            goto out_new_inode;

        err = -ENOENT;
		nvmm_establish_mapping(new_dir);
        new_de = nvmm_find_entry2(new_dir, &new_dentry->d_name, &new_pde);
        if(!new_de)
            goto out_new_dir;
        nvmm_set_link(new_dir, new_de, old_inode, 1);
        new_inode->i_ctime = CURRENT_TIME_SEC;
        if(dir_de)
            drop_nlink(new_inode);
        inode_dec_link_count(new_inode);
    } else {
    	nvmm_establish_mapping(new_dir);
        err = nvmm_add_link(new_dentry, old_inode);
        if(err)
            goto out_new_dir;
        if(dir_de)
            inode_inc_link_count(new_dir);
    }

    old_inode->i_ctime = CURRENT_TIME_SEC;
    mark_inode_dirty(old_inode);
	
	/* when you want to delete the dentry, you must change the prev dentry 'rec_len,
       so, the old_pde is aim to find the prev dentry
    */
	old_de = nvmm_find_entry2(old_dir, &old_dentry->d_name, &old_pde);
    nvmm_delete_entry(old_de, &old_pde, old_dir);

    if(dir_de){
        if(old_dir != new_dir)
            nvmm_set_link(old_inode, dir_de, new_dir, 0);
        inode_dec_link_count(old_dir);
    }
	
	if(exist)
		nvmm_destroy_mapping(new_inode);
	if(isdir)
		nvmm_destroy_mapping(old_inode);
	
	nvmm_destroy_mapping(old_dir);
	nvmm_destroy_mapping(new_dir);
    return 0;

 out_new_dir:
 	nvmm_destroy_mapping(new_dir);
	
 out_new_inode:   
 	if(exist)
    nvmm_destroy_mapping(new_inode);
    
 out_old_inode:
 	if(isdir )
    nvmm_destroy_mapping(old_inode);
	
 out_old_dir:
    nvmm_destroy_mapping(old_dir);
    return err;
}






const struct inode_operations nvmm_dir_inode_operations = {

	.create		= nvmm_create,
	.lookup		= nvmm_lookup,
	.link		= nvmm_link,
	.unlink		= nvmm_unlink,
	.symlink	= nvmm_symlink,
	.mkdir		= nvmm_mkdir,
	.rmdir		= nvmm_rmdir,
	.mknod		= nvmm_mknod,
    .rename		= nvmm_rename,
#ifdef CONFIG_NVMMFS_XATTR
#endif
	.setattr	= nvmm_notify_change,
	.get_acl	= nvmm_get_acl,
	.tmpfile	= nvmm_tmpfile,
};
