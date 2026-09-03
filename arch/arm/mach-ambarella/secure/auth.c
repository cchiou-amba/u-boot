#include <string.h>
#include <stdio.h>
#include <asm/arch-ambarella/key_alg.h>

#if defined(CFG_SECURE_BOOT)
#include "incbin.h"

#ifndef CFG_BOOTIMG_AUTH_PUBKEY
#error "CFG_BOOTIMG_AUTH_PUBKEY is not defined"
#endif
DEFINE_INCBIN(bootimg_authkey,CFG_BOOTIMG_AUTH_PUBKEY);

extern int ambarella_is_secure_boot(void);

extern int ed25519_sha512_verify(const uint8_t *message, uint32_t message_len,
    const uint8_t *signature, const uint8_t *public_key);

int auth_verify_image(void *image, unsigned long imglen)
{
	int ret;

	if (!ambarella_is_secure_boot()) {
		printf("not secure boot, skip auth_verify_image \n");
		return 0;
	}

	printf("auth_image:  ");
	ret = ed25519_sha512_verify(image,
		(imglen - SIGNATURE_SIZE),
		(image + imglen - SIGNATURE_SIZE),
		(const uint8_t *)bootimg_authkey_start);
	if (ret != 0)
		printf("failed \n");
	else
		printf("ok \n");

	return ret;
}
#else
int auth_verify_image(void *image, unsigned long imglen)
{
	return 0;
}
#endif
