/*
 * Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/*!
 * @file  remote_content_provider.h
 * @brief Data structures and definitions for Remote Content Provider
 */

#ifndef REMOTE_CONTENT_PROVIDER_H_
#define REMOTE_CONTENT_PROVIDER_H_

/* ------------------------ Drive Update Includes --------------------------- */
#include "ducommon.h"
#include "dulink.h"
#include "dulog.h"
#include "utils.h"
#include "ducmd_parser.h"
#include "dujsonparser.h"
#include "duplugin.h"
#include "dutransport.h"
#include "dulink.h"

/* ------------------------ System Includes --------------------------------- */
#include <stdbool.h>

/* ------------------------ Defines ----------------------------------------- */
// Used to init DU Link connection
#define CONTENT_SERVER_NAME           "content"
#define CONTENT_SERVER_PARENT_PATH    "/"

#define MAX_CMD_TOKENS                (20)

#define CONTENT_ERR_GENERIC           (1U)
#define CONTENT_ERR_INVALID_ARGUMENT  (2U)
#define CONTENT_ERR_NOT_FOUND         (3U)
#define CONTENT_ERR_BUFFER_TOO_SMALL  (4U)
#define CONTENT_ERR_CURL              (5U)

#define CONTENT_FILE_PATH             "files"
#define CONTENT_FILE_FULL_PATH        CONTENT_SERVER_PARENT_PATH \
                                      CONTENT_SERVER_NAME "/" \
                                      CONTENT_FILE_PATH "/"

#define FILE_LIST_NAME                "list_of_files.json"
#define FILE_LIST_MAX_SIZE            (DU_64KB)
#define FILE_LSIT_MAX_TOKEN           (2048)

#define REMOTE_SIZE_MAX               (100)

#define HTTP_OK                       (200)
#define HTTP_PARTIAL_CONTENT          (206)

#define DULINK_RETRY_NUMBER           (10U)
/* ------------------------  Data Structures -------------------------------- */
typedef struct CONTENT_SERVER CONTENT_SERVER, *PCONTENT_SERVER;

struct CONTENT_SERVER
{
    /// Mutex lock for shared resourses
    pthread_mutex_t mutex;
    /// Current command
    char cmd[DU_MAX_CMD_LEN];
    /// Current state
    uint8_t state;
    char stateStr[DU_STR_SHORT_BUF_SIZE];
    /// Current runlevel
    DU_RUN_LEVEL currentRl;
    /// Pending runlevel
    DU_RUN_LEVEL pendingRl;
    /// Most recently requested rl
    DU_RUN_LEVEL requestedRl;
    /// Local directory of content currently served
    char localPath[DULINK_MAX_PATH];
};

/// Global installer context;
extern CONTENT_SERVER content_server;

/* ------------------------ Callbacks --------------------------------------- */
uint64_t writeMemoryCB
(
    void    *contents,
    uint64_t size,
    uint64_t nmemb,
    void    *userp
);

DU_RCODE requestedRLCB
(
    const char          *pRequestPath,
    const char          *pOriginPath,
    void                *pCtx,
    uint64_t             offset,
    uint64_t             length,
    void                *pBuf,
    DULINK_CB_OPERATION  operation,
    uint64_t            *pRetVal
);

DU_RCODE cmdCB
(
    const char          *pRequestPath,
    const char          *pOriginPath,
    void                *pCtx,
    uint64_t             offset,
    uint64_t             length,
    void                *pBuf,
    DULINK_CB_OPERATION  operation,
    uint64_t            *pRetVal
);

/*!
 * @brief Internal call back for exported dulink file linked with local file
 *
 * @param[in] pRequestPath
 *          Full path of file being accessed
 *
 * @param[in] pOriginPath
 *          Full path of element originating the request
 *
 * @param[in] pCtx
 *          Pointer to global content server data structure
 *
 * @param[in] offset
 *          Offset of IO operation. This parameter would not be valid for
 * DULINK_CB_SIZE operation
 *
 * @param[in] length
 *          Size wanted to read/write. This parameter would not be valid for
 * DULINK_CB_SIZE operation
 *
 * @param[in,out] pBuf
 *          Pointer to buffer provided for read/write operation. This
 * parameter would not be valid for DULINK_CB_SIZE operation
 *
 * @param[in] operation
 *          IO operation, it would be one of:
 *          - DULINK_CB_READ
 *          - DULINK_CB_WRITE
 *          - DULINK_CB_SIZE
 *
 * @param[out] pRetVal
 *          - for operation DULINK_CB_READ Actual bytes read
 *          - for operation DULINK_CB_SIZE size of the file
 *
 * @return DU_OK
 *          Upon success
 *
 * @return DULINK_CB_ERR_INVALID_ARGUMENT
 *          Invalid input or content server state (not hosting files)
 *
 * @return DULINK_CB_ERR_IO
 *          Error when performing local disk operations
 */
DU_RCODE exportFileCB
(
    const char          *pRequestPath,
    const char          *pOriginPath,
    void                *pCtx,
    uint64_t             offset,
    uint64_t             length,
    void                *pBuf,
    DULINK_CB_OPERATION  operation,
    uint64_t            *pRetVal
);

/*!
 * Callback for handling of persistent_ctx_path node. This callback is currently
 * doing nothing
 *
 * @param[in] pRequestPath
 *          Full path of node being accessed
 * @param[in] pOriginPath
 *          Full path of element originating the request
 * @param[in] pCtx
 *          NULL
 * @param[in] offset
 *          Should always be 0, otherwise error
 * @param[in] length
 *          Length of buffer for read/write
 * @param[in] pBuf
 *          Pointer to buffer for read/write
 * @param[in] operation
 *          IO operation, one of:
 *          - DULINK_CB_READ
 *          - DULINK_CB_SIZE
 *          - DULINK_CB_WRITE
 * @param[out] pRetVal
 *          - for operation DULINK_CB_READ, Actual bytes read to buffer
 *          - for operation DULINK_CB_SIZE, length of the string
 *          - for operation DULINK_CB_WRITE, Actual bytes handled
 *
 * @return DU_OK
 *          Upon success
 */
DU_RCODE contentPersistCtxCB
(
    const char          *pRequestPath,
    const char          *pOriginPath,
    void                *pCtx,
    uint64_t             offset,
    uint64_t             length,
    void                *pBuf,
    DULINK_CB_OPERATION  operation,
    uint64_t            *pRetVal
);
/* ------------------------ Public Functions -------------------------------- */
/*!
 * Set content server to a particular state. Mutex should be held before calling
 * this function
 *
 * @param[in,out] pServer
 *      Pointer to content server data structure
 *
 * @param[in] state
 *      State to be set
 */
void contentServerSetState
(
    PCONTENT_SERVER pServer,
    uint8_t         state
);

/*!
 * Initialize content server data structures and export DU Link Nodes
 *
 * @param[in] pServer
 *      Pointer to uninitialized content server data structure
 *
 * @return DU_OK
 *      Initialized successfully
 * @return CONTENT_ERR_GENERIC
 *      Failed to initialize
 */
DU_RCODE contentServerInit
(
    PCONTENT_SERVER pServer
);

/*!
 * Main loop for content server
 *
 * @param[in] pServer
 *      Pointer to content server data structure
 *
 * @return DU_OK
 *      Exiting normally
 * @return CONTENT_ERR_GENERIC
 *      Error happened
 */
DU_RCODE contentServerRun
(
    PCONTENT_SERVER pServer
);
/* ------------------- Static Inline Functions------------------------------- */
static inline DU_RCODE
ui64ToStr
(
    uint64_t  value,
    char     *pBuf,
    uint32_t  bufSize,
    uint32_t  radix,
    uint32_t *pSize
)
{
    uint32_t len = 0U;
    uint32_t i;
    uint64_t v;
    char     tmp;
    char     table[16] = "0123456789ABCDEF";

    DU_ASSERT( pBuf != NULL && pSize != NULL);

    if (radix > 16U || radix == 0U)
    {
        DU_ERR("Only support radix <= 16 and radix > 0\n");
        return CONTENT_ERR_INVALID_ARGUMENT;
    }

    if (value == 0U)
    {
        pBuf[0] = '0';
        pBuf[1] = '\0';
        *pSize = 2U;
        return  DU_OK;
    }

    for (v = value; v != 0U; v = v/radix)
    {
        if (len >= bufSize)
        {
            DU_ERR("Buf too small\n");
            return CONTENT_ERR_BUFFER_TOO_SMALL;
        }
        pBuf[len++] = table[v%radix];
    }

    for (i = 0U; i < len/2U; i++)
    {
        tmp = pBuf[i];
        pBuf[i] = pBuf[len - i - 1U];
        pBuf[len - i - 1U] = tmp;
    }
    pBuf[len] = '\0';
    *pSize = len + 1U;

    return DU_OK;
}
#endif
