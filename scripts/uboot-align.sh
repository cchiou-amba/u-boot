#!/bin/bash

#align u-boot.bin to 32byte
obj=$1
#filesize=`stat -c %s $2`
filesize=`ls -la $2 |awk '{print $5}'`
echo "uboot size: $filesize"
let "cnt=$filesize>>5"
echo "cnt: $cnt"
let cnt++
echo "cnt: $cnt"
dd if=$obj/$2 of=$obj/$3 count=$cnt bs=32 conv=sync
