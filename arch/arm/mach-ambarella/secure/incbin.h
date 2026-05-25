/**
 * incbin.h
 *
 * Copyright (c) 2026 Ambarella International LP
 *
 * License Identifier: AMBARELLA-2-Clause
 *
 */

#ifndef __INCBIN_H__
#define __INCBIN_H__

#define _STR(x) #x
#define STR(x) _STR(x)

// Helper macro to ensure proper string expansion
#define INCBIN_FILE(x) STR(x)

#define DEFINE_INCBIN(name, file)                         	     \
    __asm__(                                                         \
        ".section .rodata\n\t"                                       \
        ".align 4\n\t"                                               \
        ".global " #name "_start\n\t"                                \
        ".global " #name "_end\n\t"                                  \
        #name "_start:\n\t"                                          \
        ".incbin \"" STR(file) "\"\n\t"                              \
        ".type " #name "_start, @object\n\t"                         \
        ".size " #name "_start, . - " #name "_start\n\t"             \
        #name "_end:\n\t"                                            \
    );                                                               \
    extern const unsigned char name##_start[];                       \
    extern const unsigned char name##_end[];

#endif
