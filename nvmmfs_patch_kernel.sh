#!/bin/bash

LINUXDIR=$1

usage () {
	echo "Usage:$0 kernelpath"
	echo "How can you expect me to patch the kernel without providing the kernel source path!!!"
	exit 1
}

if [ -z $LINUXDIR ]
then
	usage;
fi

if [ ! -f $LINUXDIR/Makefile ]
then
	echo "Directory $LINUXDIR does not exist or is not kernel source directory";
	exit 1;
fi

MAKEFILE=$LINUXDIR/fs/Makefile

echo "Updating $MAKEFILE"
echo -e "obj-\$(CONFIG_SIMFS) \t\t+= simfs/" >> $MAKEFILE

SIMFSDIR=$LINUXDIR/fs/simfs
PGTABLE_64_TYPES_H_DIR=$LINUXDIR/arch/x86/include/asm/
GFP_H_DIR=$LINUXDIR/include/linux/
INIT_C_DIR=$LINUXDIR/arch/x86/mm/
MMZONE_H_DIR=$LINUXDIR/include/linux/
PAGE_FLAGS_LAYOUT_H_DIR=$LINUXDIR/include/linux/
PAGE_ALLOC_C_DIR=$LINUXDIR/mm/
FS_H_DIR=$LINUXDIR/include/linux/
OPEN_C_DIR=$LINUXDIR/fs/
KCONFIG=$LINUXDIR/fs/Kconfig
KCONFIGOLD=$LINUXDIR/fs/Kconfig.pre.simfs

if [ ! -d $PGTABLE_64_TYPES_H_DIR ]
then
	mkdir -p $PGTABLE_64_TYPES_H_DIR
fi	

if [ ! -d $GFP_H_DIR ]
then
	mkdir -p $GFP_H_DIR
fi	

if [ ! -d $INIT_C_DIR ]
then
	mkdir -p $INIT_C_DIR
fi	

if [ ! -d $MMZONE_H_DIR ]
then
	mkdir -p $MMZONE_H_DIR
fi	

if [ ! -d $PAGE_FLAGS_LAYOUT_H_DIR ]
then
	mkdir -p $PAGE_FLAGS_LAYOUT_H_DIR
fi	

if [ ! -d $PAGE_ALLOC_C_DIR ]
then
	mkdir -p $PAGE_ALLOC_C_DIR
fi

if [ ! -d $FS_H_DIR ]
then
	mkdir -p $FS_H_DIR
fi	

if [ ! -d $OPEN_C_DIR ]
then
	mkdir -p $OPEN_C_DIR
fi

if [ -r $SIMFSDIR ]
then
	echo "$SIMFSDIR exists, so not patching. If you want to replace what is"
	echo "already there and then delete $SIMFSDIR and re-run this script"
	echo "eg.\"rm -rf $SIMFSDIR \""
else
	echo "Making directory and coping files.........."
	mkdir $LINUXDIR/fs/simfs
	cp $PWD/Makefile $LINUXDIR/fs/simfs/Makefile
	cp $PWD/Kconfig  $LINUXDIR/fs/simfs/Kconfig
	cp $PWD/files/pgtable_64_types.h $PGTABLE_64_TYPES_H_DIR
	cp $PWD/files/gfp.h $GFP_H_DIR
	cp $PWD/files/init.c $INIT_C_DIR
	cp $PWD/files/mmzone.h $MMZONE_H_DIR
	cp $PWD/files/page-flags-layout.h $PAGE_FLAGS_LAYOUT_H_DIR
	cp $PWD/files/page_alloc.c $PAGE_ALLOC_C_DIR
	cp $PWD/files/fs.h $FS_H_DIR
	cp $PWD/files/open.c $OPEN_C_DIR

	# echo "Removing tempfiles........."
	# rm $PWD/pgtable_64_types.h $PWD/gfp.h $PWD/init.c $PWD/mmzone.h $PWD/page-flags-layout.h $PWD/fs.h $PWD/open.c
	
	cp $PWD/*.c $LINUXDIR/fs/simfs/
	cp $PWD/*.h $LINUXDIR/fs/simfs/

	echo "Updating Kconfig........."
	mv  $KCONFIG $KCONFIGOLD
	sed -n -e "/[Ee][Xx][Oo][Ff][Ss]/,99999 ! p" $KCONFIGOLD > $KCONFIG
    echo "source \"fs/simfs/Kconfig\"" >> $KCONFIG
    sed -n -e "/[Ee][Xx][Oo][Ff][Ss]/,99999 p" $KCONFIGOLD >> $KCONFIG
	rm -rf $KCONFIGOLD
fi


