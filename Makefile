#
# Makefile for the linux pram-filesystem routines.
#
ccflags-y += -O0
#obj-y += nvmmfs.o
obj-$(CONFIG_SIMFS) += simfs.o

simfs-y := acl.o super.o inode.o balloc.o dir.o namei.o symlink.o file.o pgtable.o ioctl.o nvmalloc.o


