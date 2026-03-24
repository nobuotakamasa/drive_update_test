/*
 * Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/*!
 * @file  platform.h
 * @brief OS porting layer of DU
 */

#ifndef PLATFORM_H_
#define PLATFORM_H_

/* ------------------------ System includes --------------------------------- */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ------------------------ Defines ----------------------------------------- */
#if defined(__QNX__) && !defined(CONFIG_OS_QNX)
    #define CONFIG_OS_QNX
#endif

#if defined(CONFIG_OS_QNX) && defined(CONFIG_OS_LINUX)
    #error "Can't define two platforms at the same time!"
#endif

#ifndef CONFIG_OS_QNX
#define EOK (0)
#define NO_STRL_FUNC
#endif

#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 38)
#undef NO_STRL_FUNC
#endif

/* ------------------------ Platform related includes ----------------------- */
#ifdef CONFIG_OS_QNX
#include <sys/neutrino.h>
#include <sys/slog.h>
#include <sys/slogcodes.h>
#include <sys/procmgr.h>
#else
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#endif

/* ------------------------ Porting Layer ----------------------------------- */
#ifndef CONFIG_OS_QNX
#define slogf(opcode, level, fmt, ...) fprintf(stdout, fmt, ##__VA_ARGS__)
#endif

#ifdef NO_STRL_FUNC
static inline size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t lenSrc = strlen(src);
    size_t lenCpy;

    if (size > 0) {
        lenCpy = lenSrc < size - 1 ? lenSrc : size - 1;
        memcpy(dst, src, lenCpy);
        dst[lenCpy] = '\0';
    }

    return lenSrc;
}

static inline size_t strlcat(char *dst, const char *src, size_t size)
{
    size_t lenSrc = strlen(src);
    size_t lenDst = strlen(dst);
    size_t lenCpy;

    if (size > (lenDst + 1)) {
        lenCpy = lenSrc < (size - lenDst - 1) ? lenSrc : size - lenDst - 1;
        memcpy(dst + lenDst, src, lenCpy);
        dst[lenDst + lenCpy] = 0;
    }

    return lenDst + lenSrc;
}
#endif

#endif
