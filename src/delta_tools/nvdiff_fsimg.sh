#!/bin/bash

# Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

if [ $# -ne 3 ]; then

	echo "Usage: ./nvdiff_fsimg.sh.sh orig_fs.img new_fs.img type(ext4/qnx6)"
	exit 1;

fi

txt_files="uniq_files_orig.txt uniq_files_new.txt delta_files_uniq.txt delta_common_files.txt"

# Make sure nvdiff, readlink binaries are in the PWD
export PATH=$PATH:$PWD

orig=targetfs_orig
new=targetfs_new

if [ "$3" = "ext4" ]; then
    UMOUNT='sudo umount'
    DIFF='sudo diff --no-dereference'
    SYNC='sync'
elif [ "$3" = "qnx6" ]; then
    UMOUNT='umount'
    DIFF='diff'
    SYNC=''
fi

cleanup() {
    $UMOUNT $orig 2>/dev/null
    $UMOUNT $new 2>/dev/null
    rm -rf $orig
    rm -rf $new
    rm -f $txt_files
    rm -f copy_fs.sh
}

# clean up before start
cleanup

mkdir -p $orig
mkdir -p $new

# Copy the old fs to a old_fs_mod which will be modified and diff'ed
# against old_fs
cp -p $1 $1_mod.img

# mount the fs and generate the diff
if [ "$3" = "ext4" ]; then
# Case where old fs and new fs sizes are different
  oldfs_size=`stat -c %s $1`
  newfs_size=`stat -c %s $2`
  isResizeLater=0
  if [ "$oldfs_size" -gt "$newfs_size" ]; then
    isResizeLater=1
  else
	newfs_sizekb=$(( newfs_size / 1024 ))
	e2fsck -f $1_mod.img >&- 2>&-
	resize2fs -f $1_mod.img ${newfs_sizekb}K >&- 2>&-
  fi

  ls -al $1 $1_mod.img $2

# mount the fs and generate the diff
  sudo mount -o rw,loop $1_mod.img $orig
  sudo mount -o ro,loop $2 $new
elif [ "$3" = "qnx6" ]; then
  mount -t qnx6 -omntperms=0770,sync=mandatory $1_mod.img $orig
  mount -t qnx6 -oro $2 $new
else
  echo "Unsupported file system format: $3, only ext4 & qnx6 are supported"
  cleanup
  exit 1
fi

echo "Generating the diff ..."

# Get the list of all deltas between two file systems
$DIFF -rq $orig $new > delta_files_uniq.txt

if [ ! -s delta_files_uniq.txt ]; then
  echo "No changes found"
  rm -f $1_mod.img
  cleanup
  exit 1
fi

# Files/folder/links that are present in the old file system
cat delta_files_uniq.txt | grep "Only" | grep $orig | awk '{print $3 $4}' | sed 's/:/\//' > uniq_files_orig.txt
# Files/folder/links that are present in the new file system
cat delta_files_uniq.txt | grep "Only" | grep $new |  awk '{print $3 $4}' | sed 's/:/\//' > uniq_files_new.txt

# Files/folders that differ between old and new file system
cat delta_files_uniq.txt | grep "differ" |  grep -v "Symbolic links" | awk '{print $4}' > delta_common_files.txt
# Get the symbolic links that differ
cat delta_files_uniq.txt | grep "differ" | grep "Symbolic links" |  awk '{print $5}' >> delta_common_files.txt
# Files/links that change to links/files
cat delta_files_uniq.txt | grep "while" | awk '{print $9}' >> delta_common_files.txt

# Create copy_fs.sh to remove old unique files
# Copy files from new fs to old fs that differ
# Udpate symbolic links etc
echo "#!/bin/bash" >> copy_fs.sh


echo >> copy_fs.sh

# First delete all old unwanted files

while IFS= read line
do
	islink=`readlink $line`
	if [ "$islink" = "" ] && [ -f "$line" ] || [ -d "$line" ]; then
		echo "find "$line" | xargs rm -rvf" >> copy_fs.sh
	fi
	if [ -L $line ]; then
		echo "rm -rvf "$line" "$islink"" >> copy_fs.sh
	fi
done <uniq_files_orig.txt

#Copy all new files that differ
while IFS= read line
do
	path_orig=`echo "$line" | sed 's/'$new'/'$orig'/'`

	# Remove the files in the original path unconditionally.
	echo "rm -rf "$path_orig"" >> copy_fs.sh
	echo "$SYNC" >> copy_fs.sh
	islink=`readlink $line`
	# The file is a symbolic, link use cp -P
	if [ "$islink" != "" ]; then
		echo "cp -vPp "$line" "$path_orig"" >> copy_fs.sh
	else
		echo "cp -fprv "$line" "$path_orig"" >> copy_fs.sh
		echo "$SYNC" >> copy_fs.sh
	fi
done<delta_common_files.txt

# copy all new files

while IFS= read line
do
	path_orig=`echo "$line" | sed 's/'$new'/'$orig'/'`
	islink=`readlink $line`
	# create directory path if not present in the old rootfs
	dirname_f=`dirname $path_orig`
	if [ -z $dirname_f ]; then
		mkdir -p $dirname_f
	fi

	if [ "$islink" != "" ]; then
		echo "cp -Ppv "$line" "$path_orig"" >> copy_fs.sh
	else
		if [ "$3" = "qnx6" ]; then
			echo "cp -sprv "$line" "$path_orig"" >> copy_fs.sh
			echo "$SYNC" >> copy_fs.sh
		elif [ "$3" = "ext4" ]; then
			echo "cp -prv "$line" "$path_orig"" >> copy_fs.sh
		fi
	fi
done<uniq_files_new.txt

echo -e "\b\b\b [  OK  ]"

echo "Generating Copying the files over ..."

# run the copy script
sh copy_fs.sh

# Update the permissions and owner on the new_fs to match the 2nd image
DEST_PATH=$(realpath $orig)
cd $new
for FILE_NAME in $(find * 2>/dev/null); do
    if [ -f "$DEST_PATH/$FILE_NAME" ]; then
        ORIG_TEXT=$(stat -c "%u:%g" -- $FILE_NAME)
        DEST_TEXT=$(stat -c "%u:%g" -- $DEST_PATH/$FILE_NAME)
        if [ "$ORIG_TEXT" != "$DEST_TEXT" ]; then
            echo "Changing owner of $FILE_NAME"
            chown -h $ORIG_TEXT $DEST_PATH/$FILE_NAME
        fi
        ORIG_TEXT=$(stat -c "%a" -- $FILE_NAME)
        DEST_TEXT=$(stat -c "%a" -- $DEST_PATH/$FILE_NAME)
        if [ "$ORIG_TEXT" != "$DEST_TEXT" ]; then
            echo "Changing permissions on $FILE_NAME"
            chmod $ORIG_TEXT $DEST_PATH/$FILE_NAME
        fi
    fi
done
cd -

echo -e "\b\b\b [  OK  ]"

# Delete device files
if [ "$3" = "ext4" ]; then
sudo find . $orig -type c -delete 2>&1 > /dev/null
sudo find . $new -type c -delete  2>&1 > /dev/null
fi

# check to make sure if directory contents are same
result=`$DIFF -rq $orig $new`

if [ "$result" != "" ]; then

	echo "The File System contents are not same... Failed to generate delta image";
    echo $result
    cleanup
    rm -f $1_mod.img
	exit 1

fi

# clean up
cleanup

echo "Generating delta_fs.img ..."

if [ "$isResizeLater" = "1" ]; then
  newfs_size=`stat -c %s $2`
  newfs_sizekb=$(( newfs_size / 1024 ))
  e2fsck -f $1_mod.img >&- 2>&-
  resize2fs -f $1_mod.img ${newfs_sizekb}K >&- 2>&-
fi

# blocksize = 4KB.
blocksize=4096
nvdiff $1 $1_mod.img delta_fs.img $blocksize

echo -e "\b\b\b [  OK  ]"
echo "Keep "$1_mod.img
