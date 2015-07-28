/*
 * linux/fs/nvmm/wprotect.h
 *
 * Copyright (C) 2013 College of Computer Science,
 * Chonqing University
 *
 */

#ifndef __WPROTECT_H
#define __WPROTECT_H

/* nvmm_memunlock_super() before calling! */
static inline void nvmm_sync_super(struct nvmm_super_block *ns)
{
	u32 crc = 0;
	ns->s_wtime = cpu_to_be32(get_seconds());
	ns->s_sum = 0;
	crc = crc32(~0, (__u8 *)ns + sizeof(__le32), NVMM_SB_SIZE - sizeof(__le32));
	ns->s_sum = cpu_to_le32(crc);
	/* Keep sync redundant super block */
	memcpy((void *)ns + NVMM_SB_SIZE, (void *)ns, NVMM_SB_SIZE);
}

/* nvmm_memunlock_inode() before calling! */
static inline void nvmm_sync_inode(struct nvmm_inode *pi)
{
	u32 crc = 0;
	pi->i_sum = 0;
	crc = crc32(~0, (__u8 *)pi + sizeof(__le32), NVMM_INODE_SIZE - sizeof(__le32));
	pi->i_sum = cpu_to_le32(crc);
}



#ifdef CONFIG_NVMM_WRITE_PROTECT

#else

#define nvmm_is_protected(sb)	0
#define nvmm_writeable(vaddr, size, rw) do {} while (0)
static inline void nvmm_memunlock_range(struct super_block *sb, void *p,
							unsigned long len) {}
static inline void nvmm_memlock_range(struct super_block *sb, void *p,
							unsigned long len) {}
static inline void nvmm_memunlock_super(struct super_block *sb,
							struct nvmm_super_block *ns) {}
static inline void nvmm_memlock_super(struct super_block *sb,
							struct nvmm_super_block *ns)
{
//	nvmm_sync_super(ns);
}
static inline void nvmm_memunlock_inode(struct super_block *sb, struct nvmm_inode *pi) {}
static inline void nvmm_memlock_inode(struct super_block *sb,
							struct nvmm_inode *pi)
{
//	nvmm_sync_inode(pi);
}
static inline void nvmm_memunlock_block(struct super_block *sb,
							void *bp) {}
static inline void nvmm_memlock_block(struct super_block *sb,
							void *bp) {}


#endif /*CONFIG_NVMM_WRITE_PROTECT*/

#endif
