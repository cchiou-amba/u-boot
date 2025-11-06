
#ifndef __UBOOT_MISC_H__
#define __UBOOT_MISC_H__

#if defined(CONFIG_AARCH64_TRUSTZONE)
#if CFG_DTB_LOAD_ADDR > 0
#define UBOOT_DTB_SPEC_ADDR  (CFG_DTB_LOAD_ADDR)
#else
#define UBOOT_DTB_SPEC_ADDR  (CFG_KERNEL_LOAD_ADDR + 64 * 0x100000)
#endif
#else
#undef UBOOT_DTB_SPEC_ADDR   /* use the embbed dtb */
#endif
#endif
