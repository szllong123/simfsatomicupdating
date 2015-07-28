/*
 * linux/fs/nvmm/xattr.h
 *
 * Copyright (C) 2013 College of Computer Science,
 * Chonqing University
 *
 */

#include <linux/init.h>
#include <linux/xattr.h>










#ifdef CONFIG_NVMMFS_XATTR




#else  //CONFIG_NVMMFS_XATTR

static inline int
nvmm_xattr_get(struct inode *inode, int name_index,
			       const char *name, void *buffer, size_t size)
{
	return -EOPNOTSUPP;
}

static inline int
nvmm_xattr_set(struct inode *inode, int name_index, const char *name,
			       const void *value, size_t size, int flags)
{
	return -EOPNOTSUPP;
}

static inline void
nvmm_xattr_delete_inode(struct inode *inode)
{
}

static inline void
nvmm_xattr_put_super(struct super_block *sb)
{
}

static inline int
init_nvmm_xattr(void)
{
	return 0;
}

static inline void
exit_nvmm_xattr(void)
{
}

#define nvmm_xattr_handlers NULL

#endif



#ifdef CONFIG_NVMMFS_SECURITY
extern int nvmm_init_security(struct inode *inode, struct inode *dir, const struct qstr *qstr);
#else
static inline int nvmm_init_security(struct inode *inode, struct inode *dir, const struct qstr *qstr)
{
	return 0;
}
#endif


