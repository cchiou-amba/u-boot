#!/bin/sh
#make distclean O=/tmp/u-boot-cv25
make ambarella_cv25_defconfig O=/tmp/u-boot-cv25/
make -j 4 O=/tmp/u-boot-cv25/
#EXT_DTB=/home/qtu/work/stable/ambarella/boards/cv25_hazelnut/cv25_hazelnut-multi.fit
cp /tmp/u-boot-cv25/u-boot.bin ~/share/
#aarch64-linux-gnu-objdump -D /tmp/u-boot/u-boot > /tmp/a
