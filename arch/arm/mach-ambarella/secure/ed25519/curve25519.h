/*******************************************************************************
 * curve25519.h
 *
 * History:
 *  2020/06/19 - [Bo-xi Chen] create file
 *
 * Copyright 2021 Ambarella International LP
 *
 * This file and its contents ("Software") are protected by intellectual
 * property rights including, without limitation, U.S. and/or foreign
 * copyrights. This Software is also the confidential and proprietary
 * information of Ambarella International LP and its licensors. You may not use, reproduce,
 * disclose, distribute, modify, or otherwise prepare derivative works of this
 * Software or any portion thereof except pursuant to a signed license agreement
 * or nondisclosure agreement with Ambarella International LP or its authorized affiliates.
 * In the absence of such an agreement, you agree to promptly notify and return
 * this Software to Ambarella International LP
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF NON-INFRINGEMENT,
 * MERCHANTABILITY, AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL AMBARELLA INTERNATIONAL LP OR ITS AFFILIATES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; COMPUTER FAILURE OR MALFUNCTION; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************/

#ifndef __CURVE25519_H__
#define __CURVE25519_H__

#define C25519_KEY_LEN_BYTES 32
#define C25519_SIGNATURE_LEN_BYTES 64

#define C25519_KEY_EXCHANGE_LENGTH 32

#define SHA512_DIGEST_LENGTH 64

#ifndef DEFINED_64BIT_TYPES
//if use stdint.h, can comment out below
typedef unsigned long long uint64_t;
typedef signed long long  int64_t;
#endif

typedef unsigned int uint32_t;
typedef signed int int32_t;

typedef unsigned short uint16_t;
typedef signed short  int16_t;

typedef unsigned char uint8_t;
typedef signed char int8_t;


#define SHA512_DIGEST_LENGTH 64

#define UL64(x) x##ULL

//#define NULL ((void *)0)

typedef struct {
    uint64_t H[8];
    uint64_t tot_num_0;
    uint64_t tot_num_1;
    uint8_t cache_buf[128];
} sha512_ctx_t;

// read big endian
#define DR_BE64(n, b, i)                    \
{                                                       \
    (n) = ((uint64_t) (b)[(i)    ] << 56)       \
        | ((uint64_t) (b)[(i) + 1] << 48)       \
        | ((uint64_t) (b)[(i) + 2] << 40)       \
        | ((uint64_t) (b)[(i) + 3] << 32)       \
        | ((uint64_t) (b)[(i) + 4] << 24)       \
        | ((uint64_t) (b)[(i) + 5] << 16)       \
        | ((uint64_t) (b)[(i) + 6] << 8)        \
        | ((uint64_t) (b)[(i) + 7]       );         \
}

// write big endian
#define DW_BE64(n, b, i)                            \
{                                                               \
    (b)[(i)    ] = (uint8_t) ((n) >> 56);     \
    (b)[(i) + 1] = (uint8_t) ((n) >> 48);     \
    (b)[(i) + 2] = (uint8_t) ((n) >> 40);     \
    (b)[(i) + 3] = (uint8_t) ((n) >> 32);     \
    (b)[(i) + 4] = (uint8_t) ((n) >> 24);     \
    (b)[(i) + 5] = (uint8_t) ((n) >> 16);     \
    (b)[(i) + 6] = (uint8_t) ((n) >> 8);      \
    (b)[(i) + 7] = (uint8_t) ((n)       );        \
}


int X25519_memcmp(const void *dst, const void *src, uint32_t n);

int ed25519_sha512_sign(uint8_t *out_sig,
    const uint8_t *message, uint32_t message_len,
    const uint8_t public_key[32], const uint8_t private_key[32]);

int ed25519_sha512_verify(const uint8_t *message, uint32_t message_len,
    const uint8_t *signature, const uint8_t *public_key);

int x25519_gen_public_key(uint8_t public[32], const uint8_t secret[32]);

int x25519_gen_shared_secret(uint8_t out_shared_secret[32],
    const uint8_t private_key[32],
    const uint8_t public_key[32]);

void ed25519_public_from_private(uint8_t out_public_key[32],
    const uint8_t private_key[32]);

#endif

