/*
 * linux/fs/nvmm/acl.h*
 * Copyright (C) 2013 College of Computer Science,
 * Chonqing University
 *
 * inode operations
 *
 */
#include <linux/posix_acl_xattr.h>

#define NVMM_ACL_VERSION   0x0001

struct nvmm_acl_entry {
	__be16		e_tag;
	__be16		e_perm;
	__be32		e_id;
};

struct nvmm_acl_entry_short {
	__be16		e_tag;
	__be16		e_perm;
};

struct nvmm_acl_header {
	__be32		a_version;
};

static inline size_t nvmm_acl_size(int count)
{
	if (count <= 4) {
		return sizeof(struct nvmm_acl_header) +
		       count * sizeof(struct nvmm_acl_entry_short);
	} else {
		return sizeof(struct nvmm_acl_header) +
		       4 * sizeof(struct nvmm_acl_entry_short) +
		       (count - 4) * sizeof(struct nvmm_acl_entry);
	}
}

static inline int nvmm_acl_count(size_t size)
{
	ssize_t s;
	size -= sizeof(struct nvmm_acl_header);
	s = size - 4 * sizeof(struct nvmm_acl_entry_short);
	if (s < 0) {
		if (size % sizeof(struct nvmm_acl_entry_short))
			return -1;
		return size / sizeof(struct nvmm_acl_entry_short);
	} else {
		if (s % sizeof(struct nvmm_acl_entry))
			return -1;
		return s / sizeof(struct nvmm_acl_entry) + 4;
	}
}

#ifdef CONFIG_NVMMFS_POSIX_ACL

/* acl.c */

extern struct posix_acl *nvmm_get_acl(struct inode *inode, int type);
extern int nvmm_acl_chmod(struct inode *);
extern int nvmm_init_acl(struct inode *, struct inode *);

#else
#include <linux/sched.h>
#define nvmm_get_acl	NULL
#define nvmm_set_acl	NULL

static inline int nvmm_acl_chmod(struct inode *inode)
{
	return 0;
}

static inline int nvmm_init_acl(struct inode *inode, struct inode *dir)
{
	return 0;
}
#endif
