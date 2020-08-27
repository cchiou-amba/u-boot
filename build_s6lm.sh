#!/bin/sh
#make distclean O=/tmp/u-boot
make ambarella_s6lm_defconfig O=/tmp/u-boot/
make -j 4 O=/tmp/u-boot/ EXT_DTB=/home/qtu/work/stable/ambarella/boards/s6lm_pineapple/s6lm_pineapple-multi.fit
cp /tmp/u-boot/u-boot.bin ~/share/
#aarch64-linux-gnu-objdump -D /tmp/u-boot/u-boot > /tmp/a
