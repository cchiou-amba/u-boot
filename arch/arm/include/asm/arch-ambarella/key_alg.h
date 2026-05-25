/**
 * Copyright (c) 2026 Ambarella International LP
 *
 * License Identifier: AMBARELLA-2-Clause
 *
 */
#ifndef __KEY_ALG_H__
#define __KEY_ALG_H__

#if defined(CONFIG_ARCH_AMBARELLA_S6LM) || \
	defined(CONFIG_ARCH_AMBARELLA_CV22) || \
	defined(CONFIG_ARCH_AMBARELLA_CV25) || \
	defined(CONFIG_ARCH_AMBARELLA_CV28)

#define KEY_ALG_RSA		1
#define RSA_PUBKEY_SIZE		256
#define RSA_SIGN_SIZE		256
#define SIGNATURE_SIZE		RSA_SIGN_SIZE
#define PUBKEY_BIN_SIZE		RSA_PUBKEY_SIZE
#else
#define KEY_ALG_ED25519		1
#define ED25519_PUBKEY_SIZE	32
#define ED25519_SIGN_SIZE	64
#define SIGNATURE_SIZE		ED25519_SIGN_SIZE
#define PUBKEY_BIN_SIZE		ED25519_PUBKEY_SIZE
#endif

extern int auth_verify_image(void *image, unsigned long imglen);

#endif	/* __KEY_ALG_H__ */
