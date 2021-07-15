LOCAL_PATH:=$(call my-dir)

ifeq ($(CONFIG_U_BOOT), y)

DTC_OPTIONS	:= -I dts -H epapr -p 0x1000
DTC_OPTIONS	+= -i $(LINUX_SRC_DIR)/arch/arm64/boot/dts/ambarella

DTC_CFLAGS	:= -E -nostdinc -D__DTS__ -U linux -x assembler-with-cpp
DTC_CFLAGS	+= -I $(LINUX_SRC_DIR)/include -I $(AMB_BOARD_DIR) -I$(AMB_BOARD_DIR)/bsp

ifeq ($(wildcard $(CONFIG_UBOOT_DEFCONFIG_STRING)),)
CONFIG_UBOOT_DEFCONFIG_STRING :=$(CONFIG_KERNEL_DEFCONFIG_STRING)
endif

u-boot-defconfig: PRIVATE_PATH:=$(LOCAL_PATH)
u-boot-defconfig:
	@$(MAKE) $(CONFIG_UBOOT_DEFCONFIG_STRING)	\
	-C $(PRIVATE_PATH)				\
	O=$(AMB_BOARD_OUT)/u-boot

u-boot-ext.dtb: $(AMB_BOARD_DIR)/bsp/$(AMB_BOARD).dts
	@mkdir -p $(AMB_BOARD_OUT)/u-boot
	@$(CC) $(DTC_CFLAGS) $< |dtc $(DTC_OPTIONS) -O dtb -o $(AMB_BOARD_OUT)/u-boot/$@
	@echo "Build $@ Done ..."

.PHONY: u-boot
u-boot: PRIVATE_PATH:=$(LOCAL_PATH)
u-boot: $(DOT_CONFIG) u-boot-defconfig ambst u-boot-ext.dtb
	@$(MAKE) -C $(PRIVATE_PATH)		\
	O=$(AMB_BOARD_OUT)/u-boot EXT_DTB=$(AMB_BOARD_OUT)/u-boot/u-boot-ext.dtb
	@echo "Build $@ Done ..."

bootloader: u-boot
endif
