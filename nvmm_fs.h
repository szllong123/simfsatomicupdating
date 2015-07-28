/*
 * linux/include/linux/nvmm_fs.h
 *
 * Copyright (C) 2013 College of Computer Science,
 * Chonqing University
 * 
 * Non-Volatile Main Memory File System 
 * A new filesystem designed by 
 * Chongqing University, College of Computer Science
 *
 * 
 */

#ifndef _LINUX_NVMM_FS_H
#define _LINUX_NVMM_FS_H

#include <linux/types.h>
#include <linux/magic.h>



/*
 * Mount flags
 */
#define NVMM_MOUNT_PROTECT		0x000001  /* Use memory protection */
#define NVMM_MOUNT_XATTR_USER		0x000002  /* Extended user attributes */
#define NVMM_MOUNT_POSIX_ACL		0x000004  /* POSIX ACL */
#define NVMM_MOUNT_XIP			0x000008  /* Execute in place */
#define NVMM_MOUNT_ERRORS_CONT		0x000010  /* Continue on errors */
#define NVMM_MOUNT_ERRORS_RO		0x000020  /* Remount fs ro on errors */
#define NVMM_MOUNT_ERRORS_PANIC		0x000040  /* Panic on errors */

typedef __le64 phy_addr_t;

/* max file size in bytes */
#define NVMM_MAX_FILE_SIZE	(1UL << 35)
/* max dir size in bytes*/
#define NVMM_MAX_DIR_SIZE	(1UL << 21)

/* Can be used to packed structure */
#define __ATTRIBUTE_PACKED_     __attribute__ ((packed))

#define NVMMFS_DATE     "2013/11/11"
#define NVMMFS_VERSION  "0.1"


#define NVMM_SB_SIZE    (144)

/* Special inode number */
#define NVMM_ROOT_INO   (1)

#define NVMM_ROOT_INODE_OFFSET    (PAGE_SIZE)
#define NVMM_MIN_BLOCK_SIZE       (PAGE_SIZE)
#define NVMM_MAX_BLOCK_SIZE       (2 * (PAGE_SIZE))

/*
 * Inode flags (GETFLAGS/SETFLAGS)
 */
#define	NVMM_SECRM_FL			FS_SECRM_FL	/* Secure deletion */
#define	NVMM_UNRM_FL			FS_UNRM_FL	/* Undelete */
#define	NVMM_COMPR_FL			FS_COMPR_FL	/* Compress file */
#define NVMM_SYNC_FL			FS_SYNC_FL	/* Synchronous updates */
#define NVMM_IMMUTABLE_FL		FS_IMMUTABLE_FL	/* Immutable file */
#define NVMM_APPEND_FL			FS_APPEND_FL	/* writes to file may only append */
#define NVMM_NODUMP_FL			FS_NODUMP_FL	/* do not dump file */
#define NVMM_NOATIME_FL			FS_NOATIME_FL	/* do not update atime */
#define NVMM_DIRSYNC_FL			FS_DIRSYNC_FL	/* dirsync behaviour (directories only) */
#define NVMM_BTREE_FL			FS_BTREE_FL		/* btree format dir */

/*
 * nvmm inode flags
 *
 * NVMM_EOFBLOCKS_FL	There are blocks allocated beyond eof
 */
#define NVMM_EOFBLOCKS_FL	0x20000000

/* Flags that should be inherited by new inodes from their parent. */
#define NVMM_FL_INHERITED (FS_SECRM_FL | FS_UNRM_FL | FS_COMPR_FL |\
			   FS_SYNC_FL | FS_NODUMP_FL | FS_NOATIME_FL | \
			   FS_COMPRBLK_FL | FS_NOCOMP_FL | FS_JOURNAL_DATA_FL |\
			   FS_NOTAIL_FL | FS_DIRSYNC_FL)

/* Flags that are appropriate for regular files (all but dir-specific ones). */
#define NVMM_REG_FLMASK (~(FS_DIRSYNC_FL | FS_TOPDIR_FL))
/* Flags that are appropriate for non-directories/regular files. */
#define NVMM_OTHER_FLMASK (FS_NODUMP_FL | FS_NOATIME_FL)

/* Inode size in bytes */
#define NVMM_INODE_SIZE     (128)
#define NVMM_INODE_BITS     (7)

#define NVMM_BLOCK_SIZE (PAGE_SIZE)

#define INODE_NUM_PER_BLOCK (32) /* 4096/128 */

/* DIR_REC_LEN */
#define	NVMM_DIR_PAD	4
#define	NVMM_DIR_ROUND	(NVMM_DIR_PAD - 1)
#define	svn NVMM_DIR_REC_LEN(name_len)	(((name_len) + 8 + NVMM_DIR_ROUND) & \
					~NVMM_DIR_ROUND)
#define	NVMM_MAX_REC_LEN	((1 << 16) - 1)

/* error code */
#define   NOALIGN       (0x10001)  /* Start data block physical addr not page align */
#define   BADINO        (0x10002)  /* Invalid inode number */
#define   PGFAILD       (0x10003)  /* Init file page table faild*/


/*
 * Structure of an inode in the NVM
 */
struct nvmm_inode {
    __le32  i_sum;          /* Checksum of this inode */
    __le32  i_mode;         /* File mode */
    __le32  i_link_counts;  /* Links count */
    __le32  i_bytes;        /* The bytes in the last block */
    __le64  i_blocks;       /* Block count */
    __le32  i_flags;        /* File flags */
    __le32  i_file_acl;     /* File access control */
    __le32  i_dir_acl;      /* Directory access control */
    __le64  i_size;         /* File size in bytes */
    __le32  i_atime;        /* Access time */
    __le32  i_ctime;        /* Creation time */
    __le32  i_mtime;        /* Modification time */
    __le32  i_dtime;        /* Deletion time */
    __le32  i_uid;          /* Ower uid */
    __le32  i_gid;          /* Group id */
    __le32  i_generation;   /* File version (for NFS) */
    __le64  i_pg_addr;      /* File page table */
    __le64  i_next_inode_offset; /* offset of the next inode */
    char    i_pad[48];      /* padding bytes */
};

/*
 * Structure of super block in NVM
 */
struct nvmm_super_block {
	__le32  s_sum;              /* Checksum of this superblock */
	__le32  s_blocksize;       /* Data block size in bytes, in beta version, it's PAGESIZE */
	__le32  s_inode_size;       /* Inode size in bytes */
	__le64  s_size;             /* The whole file system's size */
	__le64  s_inode_count;      /* The number of inodes */
	__le64  s_free_inode_count; /* Free inode counts */
	__le64  s_inode_start;      /* Start position of inode array */
	__le64  s_block_count;	    /* The number of blocks */
	__le64  s_free_block_count;	/*free num block*/
	__le64  s_free_inode_start; /* Start position of free  inode list */
    __le64  s_free_inode_hint;
    __le64  s_free_blocknr_hint;
	__le64  s_block_start;      /* Start position of data block */
	__le64  s_free_block_start; /* Start position of free block list */
	__le32  s_mtime;            /* Mount time */
	__le32  s_wtime;            /* Modification time */
	__le16  s_magic;            /* Magic number */
	char    s_volume_name[16];  /* Volume name */
	__u8    s_fs_version[16];   /* File system version */
	__u8    s_uuid[16];         /* File system universally unique identifier */
};

/*
 * Maximal count of links to a file
 */
#define NVMM_LINK_MAX 32000

/*
 * Structure of a directory entry
 */
#define NVMM_NAME_LEN   255

struct nvmm_dir_entry {
    __le64  inode;              /* Inode number */
    __le16  rec_len;            /* Directory entry length */
    __u8    name_len;           /* Name length */
    __u8    file_type;
    char    name[NVMM_NAME_LEN];/* File name */
};


/*
 * NVMM directory file types.
 */
enum {
    NVMM_FT_UNKNOWN     = 0,
    NVMM_FT_REG_FILE    = 1,
    NVMM_FT_DIR         = 2,
    NVMM_FT_CHRDEV      = 3,
    NVMM_FT_BLKDEV      = 4,
    NVMM_FT_FIFO        = 5,
    NVMM_FT_SOCK        = 6,
    NVMM_FT_SYMLINK     = 7,
    NVMM_FT_MAX
};


/*
 * NVMM_DIR_PAD defines the directory entries boundaries
 *
 * NOTE: It must be a multiple of 4
 */
#define NVMM_DIR_PAD		 	4
#define NVMM_DIR_ROUND 			(NVMM_DIR_PAD - 1)
#define NVMM_DIR_REC_LEN(name_len)	(((name_len) + 12 + NVMM_DIR_ROUND) & \
					 ~NVMM_DIR_ROUND)

/*
 * XATTR related
 */

/* Magic value in attribute blocks */
#define PRAM_XATTR_MAGIC		0xc910629e

/* Maximum number of references to one attribute block */
#define NVMM_XATTR_REFCOUNT_MAX		1024

/* Name indexes */
#define NVMM_XATTR_INDEX_USER			1
#define NVMM_XATTR_INDEX_POSIX_ACL_ACCESS	2
#define NVMM_XATTR_INDEX_POSIX_ACL_DEFAULT	3
#define NVMM_XATTR_INDEX_TRUSTED		4
#define NVMM_XATTR_INDEX_SECURITY	        5

struct nvmm_xattr_header {
	__be32	h_magic;	/* magic number for identification */
	__be32	h_refcount;	/* reference count */
	__be32	h_hash;		/* hash value of all attributes */
	__u32	h_reserved[4];	/* zero right now */
};

struct nvmm_xattr_entry {
	__u8	e_name_len;	/* length of name */
	__u8	e_name_index;	/* attribute name index */
	__be16	e_value_offs;	/* offset in disk block of value */
	__be32	e_value_block;	/* disk block attribute is stored on (n/i) */
	__be32	e_value_size;	/* size of attribute value */
	__be32	e_hash;		/* hash value of name and value */
	char	e_name[0];	/* attribute name */
};

#define NVMM_XATTR_PAD_BITS		2
#define NVMM_XATTR_PAD		(1<<NVMM_XATTR_PAD_BITS)
#define NVMM_XATTR_ROUND		(NVMM_XATTR_PAD-1)
#define NVMM_XATTR_LEN(name_len) \
	(((name_len) + NVMM_XATTR_ROUND + \
	sizeof(struct nvmm_xattr_entry)) & ~NVMM_XATTR_ROUND)
#define NVMM_XATTR_NEXT(entry) \
	((struct nvmm_xattr_entry *)( \
	  (char *)(entry) + NVMM_XATTR_LEN((entry)->e_name_len)))
#define NVMM_XATTR_SIZE(size) \
	(((size) + NVMM_XATTR_ROUND) & ~NVMM_XATTR_ROUND)

#endif /* _LINUX_NVMM_FS_H */
