#ifndef __UBOOT_FLEXFW_H__
#define __UBOOT_FLEXFW_H__

#include <linux/types.h>

#define IMAGE_HEADER_MAGIC	0x616d6261 /* 'a' 'm' 'b' 'a' */

struct fwbin {
	u32 bin_crc32;		/* Binary CRC32 Checksum */
	u32 bin_flag;		/* Binary flag */
	u64 bin_offset;		/* Binary Location offset to header */
	u64 bin_length;		/* Binary length */
	u64 load_addr;		/* Binary Loaded address in memory */
	u64 jump_addr;		/* Binary Entry address in memory */
	u8 rsvd[128-40];	/* Reserved for use in future */
};

struct fw_image_header {
	char name[32];		/* Image name */
	u32 magic;		/* The magic number */
	u32 hdr_length;		/* Header Length */
	u32 hdr_version;	/* Version number */
	u32 build_date;		/* Version date */
	u32 bin_num;		/* Image Binary number */
	u32 flag;		/* Image Flag */
	u64 partition_size; 	/* Image totoal len */
	u8 rsvd[128-64];	/* Reserved for use in future */
	struct fwbin bin[0];	/* Image Binary information */
};

extern int flexible_image_handle(void *buf);

#endif /* __UBOOT_FLEXFW_H__ */
