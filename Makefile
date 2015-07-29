#
# Makefile for the linux pram-filesystem routines.
#
ccflags-y += -O0
#obj-y += nvmmfs.o
obj-$(CONFIG_SIMFS) += simfs.o

simfs-y := super.o inode.o balloc.o dir.o namei.o symlink.o file.o pgtable.o ioctl.o nvmalloc.o

simfs-$(CONFIG_SIMFS_FS_POSIX_ACL)	+= acl.o
simfs-$(CONFIG_SIMFS_FS_SECURITY)		+= xattr_security.o
