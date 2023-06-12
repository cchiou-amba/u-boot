##
## Copyright (c) 2021 Ambarella, Inc.
##
## This file and its contents ("Software") are protected by intellectual
## property rights including, without limitation, U.S. and/or foreign
## copyrights. This Software is also the confidential and proprietary
## information of Ambarella, Inc. and its licensors. You may not use, reproduce,
## disclose, distribute, modify, or otherwise prepare derivative works of this
## Software or any portion thereof except pursuant to a signed license agreement
## or nondisclosure agreement with Ambarella, Inc. or its authorized affiliates.
## In the absence of such an agreement, you agree to promptly notify and return
## this Software to Ambarella, Inc.
##
## THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
## INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF NON-INFRINGEMENT,
## MERCHANTABILITY, AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
## IN NO EVENT SHALL AMBARELLA, INC. OR ITS AFFILIATES BE LIABLE FOR ANY DIRECT,
## INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
## (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
## LOSS OF USE, DATA, OR PROFITS; COMPUTER FAILURE OR MALFUNCTION; OR BUSINESS
## INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
## CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
## ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
## POSSIBILITY OF SUCH DAMAGE.
##
LOCAL_PATH:=$(call my-dir)

ifeq ($(CONFIG_UBOOT), y)

DT_OPTIONS	:= -I dts -H epapr -p 0x1000 \
			-Wno-unit_address_vs_reg \
			-Wno-simple_bus_reg \
			-Wno-gpios_property \
			-Wno-unit_address_format

DT_OPTIONS	+= -i $(LINUX_SRC_DIR)/arch/arm64/boot/dts/ambarella

DT_CFLAGS	:= -E -nostdinc -D__DTS__ -U linux -x assembler-with-cpp
DT_CFLAGS	+= -I $(AMB_TOPDIR)/include -I $(LINUX_SRC_DIR)/include -I$(AMB_BOARD_DIR) -I$(AMB_BOARD_DIR)/bsp

#ifeq ($(wildcard $(CONFIG_UBOOT_DEFCONFIG_STRING)),)
#CONFIG_UBOOT_DEFCONFIG_STRING :=$(CONFIG_KERNEL_DEFCONFIG_STRING)
#endif

u-boot-defconfig: PRIVATE_PATH:=$(LOCAL_PATH)
u-boot-defconfig:
	@echo $(CONFIG_UBOOT_DEFCONFIG_STRING)
	@$(MAKE) $(CONFIG_UBOOT_DEFCONFIG_STRING)	\
	-C $(PRIVATE_PATH)				\
	O=$(AMB_BOARD_OUT)/u-boot

u-boot-ext.dtb: $(AMB_BOARD_DIR)/bsp/$(AMB_BOARD).dts
	@mkdir -p $(AMB_BOARD_OUT)/u-boot
	@$(CC) $(DT_CFLAGS) $< | tee $(AMB_BOARD_OUT)/u-boot/u-boot-ext.dts | dtc $(DT_OPTIONS) -O dtb -o $(AMB_BOARD_OUT)/u-boot/$@
	@sed -i -e "s/^#.*$$//g" -e "/^$$/d" $(AMB_BOARD_OUT)/u-boot/u-boot-ext.dts
	@echo "Build $@ Done ..."

.PHONY: u-boot
u-boot: PRIVATE_PATH:=$(LOCAL_PATH)
u-boot: $(DOT_CONFIG) u-boot-defconfig u-boot-ext.dtb
	@$(MAKE) -s -C $(PRIVATE_PATH)		\
	O=$(AMB_BOARD_OUT)/u-boot EXT_DTB=$(AMB_BOARD_OUT)/u-boot/u-boot-ext.dtb
	@echo "Build $@ Done ..."
	@mkdir -p $(IMAGES_OUT_DIR)
	@install -m 755 $(AMB_BOARD_OUT)/u-boot/u-boot.bin $(IMAGES_OUT_DIR)

boot: u-boot
endif
