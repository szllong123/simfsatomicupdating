/*
 * linux/fs/nvmm/ioctl.c
 *
 * Copyright (C) 2013 College of Computer Science,
 * Chonqing University
 *
 */


#include "nvmm.h"
#include <linux/capability.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/compat.h>
#include <linux/mount.h>
#include <asm/current.h>
#include <asm/uaccess.h>


long nvmm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct nvmm_inode_info *ni = NVMM_I(inode);
	unsigned int flags;
	int ret;

	nvmm_dbg ("cmd = %u, arg = %lu\n", cmd, arg);

	switch (cmd) {
	case NVMM_IOC_GETFLAGS:
		nvmm_get_inode_flags(ni);
		flags = le32_to_cpu(ni->i_flags & NVMM_FL_USER_VISIBLE);
		return put_user(flags, (int __user *) arg);
	case NVMM_IOC_SETFLAGS: {
		unsigned int oldflags;

		ret = mnt_want_write_file(filp);
		if (ret)
			return ret;

		if (!inode_owner_or_capable(inode)) {
			ret = -EACCES;
			goto setflags_out;
		}

		if (get_user(flags, (int __user *) arg)) {
			ret = -EFAULT;
			goto setflags_out;
		}

		flags = nvmm_mask_flags(inode->i_mode, flags);

		mutex_lock(&inode->i_mutex);
		/* Is it quota file? Do not allow user to mess with it */
		if (IS_NOQUOTA(inode)) {
			mutex_unlock(&inode->i_mutex);
			ret = -EPERM;
			goto setflags_out;
		}
		oldflags = ni->i_flags;

		/*
		 * The IMMUTABLE and APPEND_ONLY flags can only be changed by
		 * the relevant capability.
		 *
		 * This test looks nicer. Thanks to Pauline Middelink
		 */
		if ((flags ^ oldflags) & (NVMM_APPEND_FL | NVMM_IMMUTABLE_FL)) {
			if (!capable(CAP_LINUX_IMMUTABLE)) {
				mutex_unlock(&inode->i_mutex);
				ret = -EPERM;
				goto setflags_out;
			}
		}

		flags = flags & NVMM_FL_USER_MODIFIABLE;
		flags |= oldflags & ~NVMM_FL_USER_MODIFIABLE;
		ni->i_flags = flags;

		nvmm_set_inode_flags(inode);
		inode->i_ctime = CURRENT_TIME_SEC;
		mutex_unlock(&inode->i_mutex);

		mark_inode_dirty(inode);
setflags_out:
		mnt_drop_write_file(filp);
		return ret;
	}
	case NVMM_IOC_GETVERSION:
		return put_user(inode->i_generation, (int __user *) arg);
	case NVMM_IOC_SETVERSION: {
		__u32 generation;

		if (!inode_owner_or_capable(inode))
			return -EPERM;
		ret = mnt_want_write_file(filp);
		if (ret)
			return ret;
		if (get_user(generation, (int __user *) arg)) {
			ret = -EFAULT;
			goto setversion_out;
		}

		mutex_lock(&inode->i_mutex);
		inode->i_ctime = CURRENT_TIME_SEC;
		inode->i_generation = generation;
		mutex_unlock(&inode->i_mutex);

		mark_inode_dirty(inode);
setversion_out:
		mnt_drop_write_file(filp);
		return ret;
	}
	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_COMPAT
long nvmm_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	/* These are just misnamed, they actually get/put from/to user an int */
	switch (cmd) {
	case NVMM_IOC32_GETFLAGS:
		cmd = NVMM_IOC_GETFLAGS;
		break;
	case NVMM_IOC32_SETFLAGS:
		cmd = NVMM_IOC_SETFLAGS;
		break;
	case NVMM_IOC32_GETVERSION:
		cmd = NVMM_IOC_GETVERSION;
		break;
	case NVMM_IOC32_SETVERSION:
		cmd = NVMM_IOC_SETVERSION;
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return nvmm_ioctl(file, cmd, (unsigned long) compat_ptr(arg));
}
#endif

