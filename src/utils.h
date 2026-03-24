/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2025, NVIDIA CORPORATION.  All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/*!
 * @file  utils.h
 * @brief Common utility functions
 */

#ifndef UTILS_H_
#define UTILS_H_

/* ------------------------ System includes --------------------------------- */
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include <nvos_static_analysis.h>

/* ------------------------ Drive Update Includes --------------------------- */
#include "ducommon.h"
#include "platform.h"
#include "dulog.h"
#include "dulink.h"

/*--------------------------Type Define---------------------------------------*/
typedef double DUfloat64_t;

/* ------------------------ Macros ------------------------------------------ */
// The radix use to convert uint64 to string
#define RADIX_DECIMAL         (10U)
#define RADIX_HEXADECIMAL     (16U)

// The number to retry when dulinkRead or dulinkGetSize a file from
// remote-content-provider return DULINK_CB_ERR_BUSY_RETRY
#define DULINK_RETRY_SLEEP_SECOND           (3U)

/*! \brief Defines a macro for asserting failures. */
#define DU_CT_ASSERT(x)       _Static_assert(x, "DU_CT_ASSERT failed")
/*! \brief Defines a macro for asserts. */
#ifdef NDEBUG
#define DU_ASSERT(x)
#else
#define DU_ASSERT(x)          assert(x)
#endif

// Defines the array size
#define DU_ARRAY_SIZE(x)      (sizeof(x)/sizeof((x)[0]))

// Return min of left, right
static inline uint64_t
duMin
(
    uint64_t left,
    uint64_t right
)
{
    uint64_t ret;

    if (left < right)
    {
        ret = left;
    }
    else
    {
        ret = right;
    }
    return ret;
}

#ifdef CONFIG_OS_LINUX
    #define DU_STACK_SIZE (DU_128KB + DU_32KB)
#else
    #define DU_STACK_SIZE DU_128KB
#endif

/* ------------------------ Inline Functions -------------------------------- */

/*!
 * Helper calculate timespec structure
 *
 * @param[in] timeoutMs
 *      Specified wait time
 *
 * @param[out] pTs
 *      Pointer to timespect structure
 *
 * @return 0
 *      Upon success
 *
 * @return -1
 *      Upon other erros and overflow UINT64_MAX
 */
static inline int32_t
helpCalculateTimespecStruct
(
    uint64_t         timeoutMs,
    struct timespec *pTs
)
{
    int32_t  ret;
    uint64_t utv_sec;
    uint64_t utv_nsec;
    uint64_t remainderNs;
    uint64_t tmpMs;

    if ((pTs->tv_nsec < 0) || (pTs->tv_sec < 0))
    {
        DU_ERR("tv_nsec or tv_sec is overflow\n");
        return -1;
    }

    // Ensure tv_nsec is less than 1000ms to avoid EINVAL
    ret      = 0;
    utv_sec  = (uint64_t) pTs->tv_sec;
    utv_nsec = (uint64_t) pTs->tv_nsec;
    remainderNs = utv_nsec % 1000000U;
    tmpMs       = UINT64_MAX - (utv_nsec / 1000000U);
    if (tmpMs >= timeoutMs)
    {
        tmpMs = (utv_nsec / 1000000U) + timeoutMs;
    }
    else
    {
        tmpMs = timeoutMs - tmpMs;
        if ((UINT64_MAX / 1000U) <
           (UINT64_MAX - utv_sec - (tmpMs / 1000U)))
        {

            utv_sec += ((UINT64_MAX / 1000U) + (tmpMs / 1000U));
            tmpMs    = ((UINT64_MAX % 1000U) + (tmpMs % 1000U));
        }
        else
        {
            DU_ERR("tmpMs is overflow\n");
            ret = -1;
        }
    }

    if (ret == -1)
    {
        DU_ASSERT(0 == 1);
    }
    else
    {
        if (!AddU64(utv_sec, (tmpMs / 1000U), &utv_sec))
        {
            DU_ERR("Out of range\n");
            return -1;
        }
        utv_nsec = (((tmpMs % 1000U) * 1000000U) + remainderNs);
        if (!NvConvertUint64toInt64(utv_nsec, &pTs->tv_nsec))
        {
            DU_ERR("Out of range\n");
            return -1;
        }
        if (!NvConvertUint64toInt64(utv_sec, &pTs->tv_sec))
        {
            DU_ERR("Out of range\n");
            return -1;
        }
        ret = 0;
    }

    return ret;
}

/*!
 * Delete character from a string at a given position
 */
static inline void deleteCharAtPos
(
    char     *pStr,
    uint32_t  pos
)
{
    uint64_t len    = 0U;
    uint32_t strPos = 0U;

    len = strlen(pStr);
    DU_ASSERT(len >= pos);
    AddU32WithExit(pos, 1U, &strPos);
    SubU64WithExit(len, pos, &len);
    (void) memmove(&pStr[pos], &pStr[strPos], len);
}

/*!
 * Lock mutex lock and assert no error has occurred
 *
 * @param[in] pMutex
 *          Pointer to mutex to lock
 *
 * @return void
 *
 */
static inline void
duMutexLock
(
    pthread_mutex_t *pMutex
)
{
    int32_t ret = pthread_mutex_lock(pMutex);
    if (ret != 0)
    {
        DU_ERR("Mutex lock failed, ret = %d\n", ret);
        DU_ASSERT(0 == 1);
    }
}

/*!
 * Unlock mutex lock and assert no error has occurred
 *
 * @param[in] pMutex
 *          Pointer to mutex to unlock
 *
 * @return void
 *
 */
static inline void
duMutexUnlock
(
    pthread_mutex_t *pMutex
)
{
    int32_t ret = pthread_mutex_unlock(pMutex);
    if (ret != 0)
    {
        DU_ERR("Mutex unlock failed, ret = %d\n", ret);
        DU_ASSERT(0 == 1);
    }
}

/*!
 * Initialize condition variable with clock source of CLOCK_MONOTONIC.
 *
 * @param[in] pCond
 *          Pointer to the condition variable to be initialized
 *
 * @return EOK
 *          Upon successful completion
 *
 * @return Error code of POSIX thread APIs
 *          Upon other errors
 */
static inline int32_t
duCondInit
(
    pthread_cond_t  *pCond
)
{
    pthread_condattr_t attr;
    int32_t ret = EOK;

    DU_ASSERT(pCond != NULL);
    ret = pthread_condattr_init(&attr);
    if (ret == EOK)
    {
        ret = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        if (ret == EOK)
        {
            ret = pthread_cond_init(pCond, &attr);
            if (ret != EOK)
            {
                DU_ERR("Condvar init failed, ret = %d\n", ret);
            }
        }
        else
        {
            DU_ERR("Condvar setclock failed, ret = %d\n", ret);
        }
        (void)pthread_condattr_destroy(&attr);
    }
    else
    {
        DU_ERR("Condvar attr init failed, ret = %d\n", ret);
    }

    return ret;
}

/*!
 * Wait on condvar and assert no error has occurred
 *
 * @param[in] pCond
 *          Pointer to condvar to wait on
 *
 * @param[in] pMutex
 *          Pointer to mutex to release
 *
 * @return void
 *
 */
static inline void
duCondWait
(
    pthread_cond_t  *pCond,
    pthread_mutex_t *pMutex
)
{
    int32_t ret = pthread_cond_wait(pCond, pMutex);
    if (ret != 0)
    {
        DU_ERR("Condvar wait failed, ret = %d\n", ret);
        DU_ASSERT(0 == 1);
    }
}

/*!
 * Wait on condition variable with timeout. The condition variable must be
 * initialized by duCondInit() with clock source of CLOCK_MONOTONIC.
 *
 * @param[in] pCond
 *          Pointer to condvar to wait on
 *
 * @param[in] pMutex
 *          Pointer to mutex to release
 *
 * @param[in] timeoutMs
 *          Wait forever if it is UINT64_MAX, otherwise wait for the specified time
 *
 * @return EOK
 *          Upon successful completion
 *
 * @return ETIMEDOUT
 *          Function returns due to timeout
 *
 * @return -1
 *          Upon other errors
 */
static inline int32_t
duCondWaitTimed
(
    pthread_cond_t  *pCond,
    pthread_mutex_t *pMutex,
    uint64_t         timeoutMs
)
{
    struct  timespec ts;
    int32_t          ret;

    if (timeoutMs == UINT64_MAX)
    {
        ret = pthread_cond_wait(pCond, pMutex);
        if (ret != 0)
        {
            DU_ERR("Condvar wait failed, ret = %d\n", ret);
            ret = -1;
            DU_ASSERT(0 == 1);
        }
    }
    else if (timeoutMs > 0U)
    {
        ret = clock_gettime(CLOCK_MONOTONIC, &ts);
        if (ret == 0)
        {
            ret = helpCalculateTimespecStruct(timeoutMs, &ts);
            if (ret == 0)
            {
                ret = pthread_cond_timedwait(pCond, pMutex, &ts);
            }
            else
            {
                return -1;
            }
            if ((ret != 0) && (ret != ETIMEDOUT))
            {
                DU_ERR("Condvar wait failed, ret = %d\n", ret);
                ret = -1;
                DU_ASSERT(0 == 1);
            }
        }
        else
        {
            DU_ERR("Failed to get system time for duCondWaitTimed\n");
            ret = -1;
            DU_ASSERT(0 == 1);
        }
    }
    else
    {
        DU_ERR("Timeout is zero for duCondWaitTimed\n");
        ret = -1;
        DU_ASSERT(0 == 1);
    }

    return ret;
}

/*!
 * Signal condvar and assert no error has occurred
 *
 * @param[in] pCond
 *          Pointer to condvar to signal
 *
 * @return void
 *
 */
static inline void
duCondSignal
(
    pthread_cond_t *pCond
)
{
    int32_t ret = pthread_cond_signal(pCond);
    if (ret != 0)
    {
        DU_ERR("Condvar signal failed, ret = %d\n", ret);
        DU_ASSERT(0 == 1);
    }
}

/*!
 * Signal condvar to all threads waiting to be signalled
 *
 * @param[in] pCond
 *          Pointer to condvar to signal
 *
 * @return void
 *
 */
static inline void
duCondBroadcast
(
    pthread_cond_t *pCond
)
{
    int32_t ret = pthread_cond_broadcast(pCond);
    if (ret != 0)
    {
        DU_ERR("Condvar signal broadcast failed, ret = %d\n", ret);
        DU_ASSERT(0 == 1);
    }
}

/*!
 * Connect two strings and store the result into a new buffer
 *
 * @param[in, out]  pResBuf
 *          The buffer allocated by user to store the result
 *
 * @param[in]  bufSz
 *          The buffer size allocated by user
 *
 * @param[in] pStr1
 *          The first string
 *
 * @param[in]  pStr2
 *          The second string
 *
 * @return The length of the constructed string which is less than bufSz
 *          Upon success
 *
 * @return A value equal to or larger than bufSz
 *          Upon errors
 *
 */
static inline size_t
duStrCatNewBuf2
(
    char       *pResBuf,
    uint64_t    bufSz,
    const char *pStr1,
    const char *pStr2
)
{
    size_t strSz = 0U;

    DU_ASSERT(pResBuf != NULL && pStr1 != NULL && pStr2 != NULL);

    strSz = strlcpy(pResBuf, pStr1, bufSz);
    if (strSz < bufSz)
    {
        return strlcat(pResBuf, pStr2, bufSz);
    }
    else
    {
        return strSz;
    }
}

/*!
 * Connect three strings and store the result into a new buffer
 *
 * @param[in, out]  pResBuf
 *          The buffer allocated by user to store the result
 *
 * @param[in]  bufSz
 *          The buffer size allocated by user
 *
 * @param[in] pStr1
 *          The first string
 *
 * @param[in]  pStr2
 *          The second string
 *
 * @param[in]  pStr3
 *          The third string
 *
 * @return The length of the constructed string which is less than bufSz
 *          Upon success
 *
 * @return A value equal to or larger than bufSz
 *          Upon errors
 *
 */
static inline size_t
duStrCatNewBuf3
(
    char       *pResBuf,
    uint64_t    bufSz,
    const char *pStr1,
    const char *pStr2,
    const char *pStr3
)
{
    size_t strSz = 0U;

    strSz = duStrCatNewBuf2(pResBuf, bufSz, pStr1, pStr2);
    if (strSz < bufSz)
    {
        return strlcat(pResBuf, pStr3, bufSz);
    }
    else
    {
        return strSz;
    }
}

/*!
 * Connect two components of a path
 *
 * @param[in]  pPath1
 *          First path component
 *
 * @param[in]  pPath2
 *          Second path component
 *
 * @param[out] pBuf
 *          Output buffer
 *
 * @param[in]  bufSize
 *          Output buffer size
 *
 * @return Pointer to path joined string
 * @return NULL
 *          Invalid arguments or no enough buffer space
 *
 */
static inline char *
duPathJoin
(
    const char *pPath1,
    const char *pPath2,
    char       *pBuf,
    uint64_t    bufSize
)
{
    size_t      strSz;
    const char  slashStr[] = "/";
    const char *pStartPath2;

    if ((pBuf == NULL) || (pPath1 == NULL) || (pPath2 == NULL))
    {
        return NULL;
    }

    strSz = strlen(pPath2);
    if ((strSz > 0U) && (pPath2[0] == slashStr[0]))
    {
        pStartPath2 = pPath2 + 1;
    }
    else
    {
        pStartPath2 = pPath2;
    }

    strSz = strlen(pPath1);
    if ((strSz > 0U) && (pPath1[strSz - 1U] == slashStr[0]))
    {
        strSz = duStrCatNewBuf2(pBuf, bufSize, pPath1, pStartPath2);
    }
    else
    {
        strSz = duStrCatNewBuf3(pBuf, bufSize, pPath1, slashStr, pStartPath2);
    }

    if (strSz >= bufSize)
    {
        return NULL;
    }

    return pBuf;
}

/*!
 * Get Monotonic clock timestamp in milliseconds
 *
 * @param[in] pTs
 *          Pointer to struct timespec. If not NULL, pTs will be set to the
 *          current time.
 *
 * @return  Current time in millisecond
 */
static inline uint64_t
tsNow
(
    struct timespec *pTs
)
{
    struct timespec  ts;
    struct timespec *pTmpTs;
    int32_t          ret;

    if (pTs == NULL)
    {
        pTmpTs = &ts;
    }
    else
    {
        pTmpTs = pTs;
    }

    ret = clock_gettime(CLOCK_MONOTONIC, pTmpTs);
    if (ret != 0)
    {
        DU_ASSERT(0 == 1);
    }

    if ((pTmpTs->tv_sec < 0) || (pTmpTs->tv_nsec < 0))
    {
        DU_WARN("Value tv_sec or tv_nsec is overflow\n");
        DU_ASSERT(0 == 1);
    }
    else
    {
        if (((uint64_t) pTmpTs->tv_sec > (UINT64_MAX / 1000U)) ||
            ((UINT64_MAX - ((uint64_t) pTmpTs->tv_nsec / 1000000U)) <
            ((uint64_t) pTmpTs->tv_sec * 1000U)))
        {
            DU_WARN("Calculating time now has wrapped\n");
        }
        return ((uint64_t) pTmpTs->tv_sec * 1000U) +
               ((uint64_t) pTmpTs->tv_nsec / 1000000U);
    }

    return UINT64_MAX;
}

/*!
 * @brief Check if timeout has been reached
 *
 * @param[in] base
 *    Base time to calculate delay.
 *
 * @param[in] timeoutMs
 *    Timeout in milliseconds.
 *
 * @return 0 on timeout, remaining time otherwise.
 */
static inline uint64_t
duCheckTimeoutMs
(
    const uint64_t base,
    const uint64_t timeoutMs
)
{
    uint64_t now;
    uint64_t timeRemain;

    now = tsNow(NULL);
    if (now < base)
    {
        if (timeoutMs <= (UINT64_MAX - base + now))
        {
            timeRemain = 0U;
        }
        else
        {
            timeRemain = timeoutMs - (UINT64_MAX - base + now);
        }
    }
    else
    {
        if (timeoutMs <= (now - base))
        {
            timeRemain = 0U;
        }
        else
        {
            timeRemain = timeoutMs - now + base;
        }
    }
    return timeRemain;
}

/*!
 * @brief Convert a run level value to a run level enum
 *
 * @param[in] pRunlevelStr: run level value
 *
 * @return value of run level
 *          Success
 *
 * @return RL_INVALID
 *          Fail
 */
static inline DU_RUN_LEVEL
runlevelUintToEnum
(
    uint32_t runLevelValue
)
{
    DU_RUN_LEVEL ret = RL_INVALID;

    switch (runLevelValue)
    {
    case 0U:
        ret =  RL_DORMANT;
        break;
    case 1U:
        ret =  RL_MONITOR;
        break;
    case 2U:
        ret =  RL_TELECAST;
        break;
    case 3U:
        ret =  RL_XFER;
        break;
    case 4U:
        ret =  RL_BG_APPLY;
        break;
    case 5U:
        ret =  RL_FULL_APPLY;
        break;
    case 6U:
        ret =  RL_UNRESTRICTED;
        break;
    case 7U:
        ret =  RL_INVALID;
        break;
    default:
        DU_ERR("Code error. Invalid run level value.");
        ret =  RL_INVALID;
        break;
    }

    return ret;
}

/*!
 * @brief Convert a run level string to a run level value
 *
 * @param[in] pRunlevelStr: run level string
 *
 * @return value of run level
 *          Success
 *
 * @return RL_INVALID
 *          Fail
 */
static inline DU_RUN_LEVEL
runlevelStrToUint
(
    const char *pRunlevelStr
)
{
    uint32_t    i;
    const char  runLevelStr[][DU_RUNLEVEL_STR_MAX_SIZE] =
    {
        "RL_DORMANT",
        "RL_MONITOR",
        "RL_TELECAST",
        "RL_XFER",
        "RL_BG_APPLY",
        "RL_FULL_APPLY",
        "RL_UNRESTRICTED"
    };

    if (pRunlevelStr == NULL)
    {
        return RL_INVALID;
    }

    for (i = 0U; i < (uint32_t) RL_INVALID; i++)
    {
        if (strcmp(pRunlevelStr, runLevelStr[i]) == 0)
        {
            return runlevelUintToEnum(i);
        }
    }

    return RL_INVALID;
}

/*!
 * @brief Convert a run level value to a run level str
 *
 * @param[in] runlevel: run level value
 *
 * @param[out] pBuf: buf to get run level string
 *
 * @param[in] bufSize: size of buf
 *
 * @return the actually size of run level string
 *          Success
 *
 * @return 0
 *          Fail
 */
static inline uint64_t
runlevelUintToStr
(
    DU_RUN_LEVEL runlevel,
    char        *pBuf,
    uint64_t     bufSize
)
{
    uint64_t     size;
    const char   runLevelStr[][DU_RUNLEVEL_STR_MAX_SIZE] =
    {
        "RL_DORMANT",
        "RL_MONITOR",
        "RL_TELECAST",
        "RL_XFER",
        "RL_BG_APPLY",
        "RL_FULL_APPLY",
        "RL_UNRESTRICTED"
    };

    if ((pBuf == NULL) || (bufSize < DU_RUNLEVEL_STR_MAX_SIZE) ||
        (runlevel >= RL_INVALID))
    {
        return 0U;
    }

    size = strlcpy(pBuf, runLevelStr[runlevel], DU_RUNLEVEL_STR_MAX_SIZE);
    if (size >= DU_RUNLEVEL_STR_MAX_SIZE)
    {
        return 0U;
    }

    return (size + 1U);
}

/*!
 * Compute exponential function base ^ exp. Used instead of math.h version of
 * pow() to avoid typecasting. Negative exponent is not supported
 *
 * @param[in] base
 *      Base value
 * @param[in] exponent
 *      Exponent, do not support negative numbers
 *
 * @return base ^ exponent
 */
static inline uint64_t power
(
    uint64_t base,
    uint32_t exponent
)
{
    uint32_t i;
    uint64_t ret = 1L;
    for (i = 0; i < exponent; i++)
    {
        MultU64WithExit(ret, base, &ret);
    }
    return ret;
}

/*!
 * Return true if pInput ends with substring pSuffix
 *
 * @param[in] pInput
 *      Input string to check
 * @param[in] pSuffix
 *      String to search for at end of input
 *
 * @return true
 *      Input ends with suffix string
 * @return false
 *      Input does not end with suffix string
 */
static inline bool endsWithStr
(
    const char *pInput,
    const char *pSuffix
)
{
    size_t lenInput = strlen(pInput);
    size_t lenSuffix = strlen(pSuffix);
    if (lenSuffix > lenInput)
    {
        return false;
    }
    return (strncmp(pInput + lenInput - lenSuffix, pSuffix, lenSuffix) == 0);
}

/*!
 * @brief Convert a string to a value of uint64_t type.
 *
 * @param[in] pStr
 *          Pointer to the input string.
 *
 * @param[out] pResult
 *          Pointer to the converted result.
 *
 * @return 0
 *          On success
 *
 * @return -1
 *          On failure
 */
static inline int32_t duStrToUint64
(
    const char *pStr,
    uint64_t   *pResult
)
{
    char    *pEnd;
    uint64_t tmp;
    char     endChar = '\0';

    if ((pStr == NULL) || (pResult == NULL))
    {
        return -1;
    }

    errno = 0;
    tmp   = strtoul(pStr, &pEnd, 0);
    if ((errno != 0) || (pEnd == NULL) || (*pEnd != endChar) || (pStr == pEnd))
    {
        return -1;
    }
    else
    {
        *pResult = tmp;
    }

    return 0;
}

/*!
 * @brief Convert a string to a value of uint32_t type.
 *
 * @param[in] pStr
 *          Pointer to the input string.
 *
 * @param[out] pResult
 *          Pointer to the converted result.
 *
 * @return 0
 *          On success
 *
 * @return -1
 *          On failure
 */
static inline int32_t duStrToUint32
(
    const char *pStr,
    uint32_t   *pResult
)
{
    uint64_t tmp;

    if (duStrToUint64(pStr, &tmp) == -1)
    {
        return -1;
    }

    if (NvConvertUint64toUint32(tmp, pResult) == false)
    {
        return -1;
    }

    return 0;
}

/*!
 * @brief Convert a size_t to a value of uint32_t type.
 *
 * @param[in] pStr
 *          size_t type value.
 *
 * @param[out] pResult
 *          Pointer to the converted result.
 *
 * @return 0
 *          On success
 *
 * @return -1
 *          On failure
 */
static inline int32_t duSizetToUint32
(
    size_t      val,
    uint32_t   *pResult
)
{
    int32_t ret=-1;

    if (val <= UINT32_MAX)
    {
        *pResult = (uint32_t)val;
        ret=0;
    }

    return ret;
}

/*!
 * DJB2 Hash function hashing string to 64 bit hash
 *
 * @param[in] pInputStr
 *      Input string to be hashed
 *
 * @return Hash result
 */
static inline uint64_t djb2Hash
(
    const char *pInputStr
)
{
    uint64_t hash = 5381U;
    int8_t   tmpInt;

    for (;;)
    {
        tmpInt = (int8_t) (*pInputStr);
        if (tmpInt == 0)
        {
            break;
        }
        hash = ((hash << 5U) + hash) + (uint8_t)tmpInt;

        pInputStr++;
    }

    return hash;
}
/*!
 * Calculates the CRC32 of passed in buffer
 *
 * @param[in] val
 *      output from prior crc32 call. Used for chain buffers
 *
 * @param[in] pBuffer
 *      buffer to crc32
 *
 * @param[in] bufferSize
 *      length of buffer
 *
 * @return crc32
 *
 */

static inline uint32_t duCrc32
(
     uint32_t   val,
     const void *pBuffer,
     size_t     bufferSize
)
{
    /* Pre calculated modulo 2 division remainder for 256 bytes combination */
    static const uint32_t duCrc32Tab[] = {
        0x00000000U, 0x77073096U, 0xee0e612cU, 0x990951baU, 0x076dc419U,
        0x706af48fU, 0xe963a535U, 0x9e6495a3U, 0x0edb8832U, 0x79dcb8a4U,
        0xe0d5e91eU, 0x97d2d988U, 0x09b64c2bU, 0x7eb17cbdU, 0xe7b82d07U,
        0x90bf1d91U, 0x1db71064U, 0x6ab020f2U, 0xf3b97148U, 0x84be41deU,
        0x1adad47dU, 0x6ddde4ebU, 0xf4d4b551U, 0x83d385c7U, 0x136c9856U,
        0x646ba8c0U, 0xfd62f97aU, 0x8a65c9ecU, 0x14015c4fU, 0x63066cd9U,
        0xfa0f3d63U, 0x8d080df5U, 0x3b6e20c8U, 0x4c69105eU, 0xd56041e4U,
        0xa2677172U, 0x3c03e4d1U, 0x4b04d447U, 0xd20d85fdU, 0xa50ab56bU,
        0x35b5a8faU, 0x42b2986cU, 0xdbbbc9d6U, 0xacbcf940U, 0x32d86ce3U,
        0x45df5c75U, 0xdcd60dcfU, 0xabd13d59U, 0x26d930acU, 0x51de003aU,
        0xc8d75180U, 0xbfd06116U, 0x21b4f4b5U, 0x56b3c423U, 0xcfba9599U,
        0xb8bda50fU, 0x2802b89eU, 0x5f058808U, 0xc60cd9b2U, 0xb10be924U,
        0x2f6f7c87U, 0x58684c11U, 0xc1611dabU, 0xb6662d3dU, 0x76dc4190U,
        0x01db7106U, 0x98d220bcU, 0xefd5102aU, 0x71b18589U, 0x06b6b51fU,
        0x9fbfe4a5U, 0xe8b8d433U, 0x7807c9a2U, 0x0f00f934U, 0x9609a88eU,
        0xe10e9818U, 0x7f6a0dbbU, 0x086d3d2dU, 0x91646c97U, 0xe6635c01U,
        0x6b6b51f4U, 0x1c6c6162U, 0x856530d8U, 0xf262004eU, 0x6c0695edU,
        0x1b01a57bU, 0x8208f4c1U, 0xf50fc457U, 0x65b0d9c6U, 0x12b7e950U,
        0x8bbeb8eaU, 0xfcb9887cU, 0x62dd1ddfU, 0x15da2d49U, 0x8cd37cf3U,
        0xfbd44c65U, 0x4db26158U, 0x3ab551ceU, 0xa3bc0074U, 0xd4bb30e2U,
        0x4adfa541U, 0x3dd895d7U, 0xa4d1c46dU, 0xd3d6f4fbU, 0x4369e96aU,
        0x346ed9fcU, 0xad678846U, 0xda60b8d0U, 0x44042d73U, 0x33031de5U,
        0xaa0a4c5fU, 0xdd0d7cc9U, 0x5005713cU, 0x270241aaU, 0xbe0b1010U,
        0xc90c2086U, 0x5768b525U, 0x206f85b3U, 0xb966d409U, 0xce61e49fU,
        0x5edef90eU, 0x29d9c998U, 0xb0d09822U, 0xc7d7a8b4U, 0x59b33d17U,
        0x2eb40d81U, 0xb7bd5c3bU, 0xc0ba6cadU, 0xedb88320U, 0x9abfb3b6U,
        0x03b6e20cU, 0x74b1d29aU, 0xead54739U, 0x9dd277afU, 0x04db2615U,
        0x73dc1683U, 0xe3630b12U, 0x94643b84U, 0x0d6d6a3eU, 0x7a6a5aa8U,
        0xe40ecf0bU, 0x9309ff9dU, 0x0a00ae27U, 0x7d079eb1U, 0xf00f9344U,
        0x8708a3d2U, 0x1e01f268U, 0x6906c2feU, 0xf762575dU, 0x806567cbU,
        0x196c3671U, 0x6e6b06e7U, 0xfed41b76U, 0x89d32be0U, 0x10da7a5aU,
        0x67dd4accU, 0xf9b9df6fU, 0x8ebeeff9U, 0x17b7be43U, 0x60b08ed5U,
        0xd6d6a3e8U, 0xa1d1937eU, 0x38d8c2c4U, 0x4fdff252U, 0xd1bb67f1U,
        0xa6bc5767U, 0x3fb506ddU, 0x48b2364bU, 0xd80d2bdaU, 0xaf0a1b4cU,
        0x36034af6U, 0x41047a60U, 0xdf60efc3U, 0xa867df55U, 0x316e8eefU,
        0x4669be79U, 0xcb61b38cU, 0xbc66831aU, 0x256fd2a0U, 0x5268e236U,
        0xcc0c7795U, 0xbb0b4703U, 0x220216b9U, 0x5505262fU, 0xc5ba3bbeU,
        0xb2bd0b28U, 0x2bb45a92U, 0x5cb36a04U, 0xc2d7ffa7U, 0xb5d0cf31U,
        0x2cd99e8bU, 0x5bdeae1dU, 0x9b64c2b0U, 0xec63f226U, 0x756aa39cU,
        0x026d930aU, 0x9c0906a9U, 0xeb0e363fU, 0x72076785U, 0x05005713U,
        0x95bf4a82U, 0xe2b87a14U, 0x7bb12baeU, 0x0cb61b38U, 0x92d28e9bU,
        0xe5d5be0dU, 0x7cdcefb7U, 0x0bdbdf21U, 0x86d3d2d4U, 0xf1d4e242U,
        0x68ddb3f8U, 0x1fda836eU, 0x81be16cdU, 0xf6b9265bU, 0x6fb077e1U,
        0x18b74777U, 0x88085ae6U, 0xff0f6a70U, 0x66063bcaU, 0x11010b5cU,
        0x8f659effU, 0xf862ae69U, 0x616bffd3U, 0x166ccf45U, 0xa00ae278U,
        0xd70dd2eeU, 0x4e048354U, 0x3903b3c2U, 0xa7672661U, 0xd06016f7U,
        0x4969474dU, 0x3e6e77dbU, 0xaed16a4aU, 0xd9d65adcU, 0x40df0b66U,
        0x37d83bf0U, 0xa9bcae53U, 0xdebb9ec5U, 0x47b2cf7fU, 0x30b5ffe9U,
        0xbdbdf21cU, 0xcabac28aU, 0x53b39330U, 0x24b4a3a6U, 0xbad03605U,
        0xcdd70693U, 0x54de5729U, 0x23d967bfU, 0xb3667a2eU, 0xc4614ab8U,
        0x5d681b02U, 0x2a6f2b94U, 0xb40bbe37U, 0xc30c8ea1U, 0x5a05df1bU,
        0x2d02ef8dU
    };

    uint32_t finalCrc = val ^ ~0U;
    const uint8_t *pBuf = (const uint8_t *) pBuffer;

    while (bufferSize != 0U)
    {
        finalCrc = (duCrc32Tab[(finalCrc ^ *pBuf) & 0xFFU] ^
                    (finalCrc >> 8));
        pBuf++;
        bufferSize--;
    }

    return finalCrc ^ ~0U;
}

/*!
 * Compares the contents of a string and a wide string
 *
 * @param[in] pWstring
 *      wide-char string to compare
 *
 * @param[in] pString
 *      string to compare
 *
 * @return 0
 *      Strings are the same
 * @return >1
 *      pWstring is alphabetically after pString
 * @return <1
 *      pWstring is alphabetically before pString
 */
static inline int32_t duWstrStrCmp
(
    const uint16_t    *pWstring,
    const char        *pString
)
{
    int32_t intWChar = 0;
    int32_t intSChar = 0;
    int8_t  tmpInt8;

    tmpInt8  = (int8_t) (*pString);
    intSChar = (int32_t) tmpInt8;
    intWChar = CastU32toS32WithExit((uint32_t) (*pWstring));
    while(intWChar == intSChar)
    {
        if(*pWstring == 0U)
        {
            return 0;
        }
        pWstring++;
        pString++;
        tmpInt8  = (int8_t) (*pString);
        intSChar = (int32_t) tmpInt8;
        intWChar = CastU32toS32WithExit((uint32_t) (*pWstring));
    }

    if (intWChar == intSChar)
    {
        return 0;
    }
    else if (intWChar > intSChar)
    {
        return 1;
    }
    else
    {
        return -1;
    }
}

/*!
 * Convert uint16_t string to char string
 *
 * @param[out] pString
 *      Buffer to hold the converted string
 *
 * @param[in] pWstring
 *      wide-char string to convert
 *
 * @param[in] size
 *      Length of string
 *
 * @return none
 */
static inline void duWstrToStr
(
     char              *pString,
     const uint16_t    *pWstring,
     size_t             size
)
{
   size_t i;

   /* The partition names supported now are only lower case, english & UTF8
    * Remove the second byte from wide string
    */
    for (i = 0 ; i < size; i++)
    {
       if (pWstring[i] <= (uint16_t) SCHAR_MAX)
       {
           pString[i] = (char) pWstring[i];
       }
       else
       {
          // pWstring always holds UTF8, this will never reach.
          DU_ASSERT(0 == 1);
       }

       if (pWstring[i] == 0U)
       {
          break;
       }
    }
}

/*!
 * Return the array size
 *
 * @param[in] aSize
 *      Total size of array in bytes
 *
 * @param[in] elSize
 *      Element size of the array
 *
 * @return Length of the array
 */
static inline size_t duArraySize(size_t aSize, size_t elSize)
{
    if (elSize != 0U)
    {
        return (aSize / elSize);
    }

    return 0U;
}

/*!
 * Convert the input hex string into byte array.
 * @param[out] pOutData
 *      Output buffer contining converted byte array
 * @param[in] pHexSrc
 *      Hexadecimal string input
 * @param[in] len
 *      Lenght of the byte array
 * @return DU_OK
 *      pHexSrc is successfully converted to bute array
 * @return DUCOMMON_ERR_INVALID_ARGUMENT
 *      Invalid arguments for input hex string or output byte array
 */
static inline DU_RCODE
duHexStrAsByteArray
(
    uint8_t    *pOutData,
    const char *pHexSrc,
    uint32_t    len
)
{
    const char *pPos = pHexSrc;
    uint32_t    count;
    uint64_t    tmp;

    if ((pHexSrc == NULL) || ((strlen(pHexSrc) % 2U) != 0U) || (pOutData == NULL))
    {
        return DUCOMMON_ERR_INVALID_ARGUMENT;
    }

    for (count = 0U; count < len; count++)
    {
        if (pPos[0] == '\0')
        {
            break;
        }

        // Input hex string without 0x is converted to byte array
        const char utilsTok[3] = {pPos[0], pPos[1], '\0'};
        errno = 0;
        tmp = strtoul(utilsTok, NULL, 16);
        if ((errno != 0) || (tmp > (uint64_t) UINT8_MAX))
        {
            return DUCOMMON_ERR_INVALID_ARGUMENT;
        }
        pOutData[count] = (uint8_t) tmp;
        pPos += 2U * sizeof(char);
    }

    return DU_OK;
}

/*!
 * Convert the input byte array into hex string.
 *
 * @param[out] pOutData
 *      Output buffer continuing converted string
 *
 * @param[in] outSize
 *      Size of pOutData
 *
 * @param[in] pArray
 *      Input bytes array
 *
 * @param[in] arraySize
 *      Size of the byte array
 *
 * @return DU_OK
 *      Success
 * @return other
 *      Failed
 */
static inline DU_RCODE
duU8ArrayToStr
(
    char          *pOutData,
    uint32_t       outSize,
    const uint8_t *pArray,
    uint32_t       arraySize
)
{
    const char     value[RADIX_HEXADECIMAL] = {'0', '1', '2', '3', '4',
                '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    uint32_t       i;
    uint32_t       pos    = 0U;
    uint32_t       tmpPos = 0U;

    if ((pOutData == NULL) || (pArray == NULL))
    {
        return DUCOMMON_ERR_INVALID_ARGUMENT;
    }

    if (arraySize > (UINT32_MAX/2U))
    {
        DU_ERR("arraySize(%u) is too large\n", arraySize);
        return DUCOMMON_ERR_INVALID_ARGUMENT;
    }

    if ((arraySize * 2U) >= outSize)
    {
        DU_ERR("OutSize(%u) is too small, Array size:%u\n", outSize,
               arraySize);
        return DUCOMMON_ERR_INVALID_ARGUMENT;
    }

    for (i = 0U; i < arraySize ; i++)
    {
        pOutData[pos] = value[pArray[i] / RADIX_HEXADECIMAL];
        if (!AddU32(pos, 1U, &tmpPos))
        {
            DU_ERR("Out of range\n");
            return DUCOMMON_ERR_INVALID_ARGUMENT;
        }
        pOutData[tmpPos] = value[pArray[i] % RADIX_HEXADECIMAL];
        if (!AddU32(tmpPos, 1U, &pos))
        {
            DU_ERR("Exceeding the maximum value range\n");
            return DUCOMMON_ERR_INVALID_ARGUMENT;
        }
    }

    pOutData[pos] = '\0';
    return DU_OK;
}

/*!
 * Create a thread with with DU_STACK_SIZE as stack size
 *
 *@param[out] pThread
 *     pointer to the new created thread
 *@param[in] pStartRoutine
 *     the new thread starts execution by invoking
 *@param[in] pArg
 *     pArg is passed as the sole argument of pStartRoutine
 *@param[in] bJoinable
 *     true if the thread is joinable, false if detached.
 *
 *@return EOK
 *     a new thread is created successfully
 *@return not EOK
 *     invalid input parameter or error on pthread_create call
 */
static inline int32_t duCreateThread
(
    pthread_t *pThread,
    void*     (*pStartRoutine) (void *pStatArgs),
    void      *pArg,
    bool      bJoinable
)
{
    int32_t         retInt = EOK;
    const pthread_attr_t *pAttr  = NULL;
    pthread_attr_t  attr;
    int32_t         state;

    if (pStartRoutine == NULL)
    {
        DU_ERR("Invalid parameter: pStartRouting = %p\n", pStartRoutine);
        return -1;
    }

    retInt = pthread_attr_init(&attr);
    if (retInt != EOK)
    {
        DU_WARN("Ret %d on pthread_attr_init\n", retInt);
    }
    else
    {
        pAttr = &attr;
        retInt = pthread_attr_setstacksize(&attr, DU_STACK_SIZE);
        if (retInt != EOK)
        {
            DU_WARN("Ret %d on pthread_attr_setstacksize\n", retInt);
        }

        if (bJoinable)
        {
            state = PTHREAD_CREATE_JOINABLE;
        }
        else
        {
            state = PTHREAD_CREATE_DETACHED;
        }
        retInt = pthread_attr_setdetachstate(&attr, state);
        if (retInt != EOK)
        {
            DU_WARN("Ret %d on pthread_attr_setdetachstate\n", retInt);
        }
    }
    retInt = pthread_create(pThread, pAttr, pStartRoutine, pArg);
    if (retInt != EOK)
    {
        DU_ERR("Error %d on pthread_create\n", retInt);
    }

    if (pAttr != NULL)
    {
        if (pthread_attr_destroy(&attr) != 0)
        {
            DU_WARN("Error on pthread_attr_destroy\n");
        }
    }

    return retInt;
}

/*
 * @brief Convert uint64 to str base on radix, for example, value = 1234,
 *        radix = 10, then the pBuf = "1234"
 *
 * @param[in] value
 *          Value to convert
 *
 * @param[out] pBuf
 *          The convert result
 *
 * @param[in] bufSize
 *          Size of pBuf
 *
 * @param[in] radix
 *          The base to use when converting the number, should <=16
 *
 * @param[out] pSize
 *          If pSize is null, do nothing
 *          Otherwise set *pSize to chars in the pBuf, including the '\0'
 *
 * @return EOK
 *          Success
 *
 * @return Other
 *          Fail
 */
static inline int32_t
uint64ToStr
(
    uint64_t  value,
    char     *pBuf,
    uint64_t  bufSize,
    uint32_t  radix,
    uint32_t *pSize
)
{
    uint32_t       len = 0U;
    uint32_t       i;
    uint64_t       v = value;
    char           tmp;
    const char     table[16] = "0123456789abcdef";

    if ( pBuf == NULL )
    {
        return -1;
    }

    if ((radix > 16U) || (radix == 0U))
    {
        return -2;
    }

    do
    {
        if ((uint64_t)len >= bufSize)
        {
            return -3;
        }
        pBuf[len] = table[v%radix];
        len++;
        v = v/radix;
    } while (v != 0U);

    for (i = 0U; i < (len/2U); i++)
    {
        tmp = pBuf[i];
        pBuf[i] = pBuf[len - i - 1U];
        pBuf[len - i - 1U] = tmp;
    }
    pBuf[len] = '\0';
    if (pSize != NULL)
    {
        *pSize = len + 1U;
    }

    return EOK;
}

static inline DU_RCODE
dulinkGetSizeRetry
(
    const char *pPath,
    uint64_t   *pSize
)
{
    DU_RCODE ret;

    while(true)
    {
        ret = dulinkGetSize(pPath, pSize);
        if (ret == DU_OK)
        {
            break;
        }
        else if (ret == DULINK_CB_ERR_BUSY_RETRY)
        {
            DU_WARN("dulinkGetSize return %#x, sleep and retry\n", ret);
            (void) sleep(DULINK_RETRY_SLEEP_SECOND);
        }
        else
        {
            DU_ERR("Error %#x on dulinkGetSize: %s\n", ret, pPath);
            return DUCOMMON_ERR_GENERIC;
        }
    }

    return DU_OK;
}

static inline DU_RCODE
dulinkReadRetry
(
    const char *pPath,
    uint64_t    offset,
    uint64_t    length,
    void       *pBuf,
    uint64_t   *pRetLen
)
{
    DU_RCODE ret;

    while(true)
    {
        ret = dulinkRead(pPath, offset, length, pBuf, pRetLen);
        if (ret == DU_OK)
        {
            break;
        }
        else if (ret == DULINK_CB_ERR_BUSY_RETRY)
        {
            DU_WARN("dulinkRead return %#x, sleep and retry\n", ret);
            (void) sleep(DULINK_RETRY_SLEEP_SECOND);
        }
        else
        {
            DU_ERR("Error %#x on dulinkRead: %s\n", ret, pPath);
            return DUCOMMON_ERR_GENERIC;
        }
    }

    return DU_OK;
}


/*!
 * Read Plugin Node to buffer
 *
 * @param[in] pPlugin
 *      Pointer to DU-Auth plugin path
 *
 * @param[in] pNode
 *      Pointer to Node name string
 *
 * @param[out] pBuf
 *      Pointer to read buffer
 *
 * @param[in] bufSize
 *      Read buffer size
 *
 * @param[out] retLen
 *      Actual read length
 *
 * @return DU_OK
 *      Read file node success
 * @return DUMASTER_ERR_GENERIC
 *      DU path join failed
 * @return DUMASTER_ERR_BUFFER_TOO_SMALL
 *      Read buffer size is too small
 * @return DULINK ERROR
 *      Error occurred in DU Link layer
 */
static inline DU_RCODE
readFileNode
(
    const char *pPlugin,
    const char *pNode,
    char       *pBuf,
    uint64_t    bufSize,
    uint64_t   *retLen
)
{
    DU_RCODE    duRet = DU_OK;
    const char *pPath = NULL;
    char        pathBuf[DULINK_MAX_PATH] = {0};

    pPath = duPathJoin(pPlugin, pNode, pathBuf, sizeof(pathBuf));
    if (pPath == NULL)
    {
        DU_ERR("Join DU path failed\n");
        duRet = DUCOMMON_ERR_GENERIC;
    }
    else
    {
        duRet = dulinkRead(pPath, 0U, bufSize, pBuf, retLen);
        if (duRet != DU_OK)
        {
            DU_ERR("Read %s from %s failed, err:%#x\n", pNode, pPlugin, duRet);
        }
        else
        {
            if (*retLen >= bufSize)
            {
                DU_ERR("Read buffer(%lu) is too small\n", bufSize);
                duRet = DUCOMMON_ERR_BUFFER_TOO_SMALL;
            }
        }
    }

    return duRet;
}

/*!
 * Get String Size (String Length with End Charactor)
 *
 * @param[in] pStr
 *      Pointer to string
 *
 * @return String Size
 *      String length with end charactor
 */
static inline uint64_t
duStrSize
(
    const char *pStr
)
{
    uint64_t strSize = 0U;

    DU_ASSERT(pStr != NULL);
    strSize = (uint64_t)strlen(pStr);
    AddU64WithExit(strSize, 1U, &strSize);
    return strSize;
}

/*!
 * Write Plugin File Node
 *
 * @param[in] pPlugin
 *      Pointer to DU-Auth plugin path
 *
 * @param[in] pNode
 *      Pointer to Node name string
 *
 * @param[in] pCmd
 *      Pointer to Command string
 *
 * @return DU_OK
 *      Write node success
 * @return DULINK ERROR
 *      Error occurred in DU Link layer
 */
static inline DU_RCODE
writeFileNode
(
    const char *pPlugin,
    const char *pNode,
    const char *pCmd
)
{
    DU_RCODE        duRet = DU_OK;
    const char     *pPath = NULL;
    char            pathBuf[DULINK_MAX_PATH] = {0};
    char            cmdBuf[DU_MAX_CMD_LEN] = {0};
    uint64_t        retLen = 0U;
    size_t          len = 0U;

    pPath = duPathJoin(pPlugin, pNode, pathBuf, sizeof(pathBuf));
    if (pPath == NULL)
    {
        DU_ERR("Join DU path failed\n");
        duRet = DUCOMMON_ERR_GENERIC;
    }
    else
    {
        len = strlcpy(cmdBuf, pCmd, sizeof(cmdBuf));
        if (len >= sizeof(cmdBuf))
        {
            DU_ERR("Cmd buffer too small, need > %u\n", (uint32_t)len);
        }

        duRet = dulinkWrite(pPath, 0U, duStrSize(cmdBuf), (void *)cmdBuf,
                            &retLen);
        if (duRet != DU_OK)
        {
            DU_ERR("Write cmd %s to %s failed, err:%#x\n", pCmd, pPlugin, duRet);
        }
    }
    return duRet;
}

static inline
uintptr_t roundUp(uintptr_t addr, size_t align)
{
    char *r1 = (char *)addr + align -1;
    NVOS_COV_WHITELIST(false_positive, NVOS_CERT(INT30_C), "SWE-DU-194-SWSADR")
    return (uintptr_t)r1 / align * align;
}

#endif // UTILS_H_
