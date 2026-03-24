/*
 * Copyright (c) 2019-2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/*!
 * @file  content_server.h
 * @brief Data structures and definitions for Content Server
 */

#ifndef CONTENT_SERVER_H_
#define CONTENT_SERVER_H_

/* ------------------------ System Includes --------------------------------- */
#include <stdbool.h>

/* ------------------------ Drive Update Includes --------------------------- */
#include "ducommon.h"
#include "dujsonparser.h"
#include "dulink.h"
#include "plugin_common/ducontent_provider_common.h"

/* ------------------------ Defines ----------------------------------------- */
// Used to init DU Link connection
#define CONTENT_SERVER_PARENT_PATH    "/"

// Used to init DU Link connection
#define CONTENT_SERVER_NAME           "content"
#define CONTENT_FILE_PATH             "files"
#define CONTENT_FILE_FULL_PATH        CONTENT_SERVER_PARENT_PATH  \
                                      CONTENT_SERVER_NAME "/" \
                                      CONTENT_FILE_PATH "/"

#define MAX_CMD_TOKENS                (20)

#define FILE_LIST_NAME                "list_of_files.json"
#define FILE_LIST_MAX_SIZE            (DU_64KB)
#define FILE_LIST_MAX_TOKEN           (2048)
/* ------------------------  Data Structures -------------------------------- */
typedef struct CONTENT_SERVER CONTENT_SERVER, *PCONTENT_SERVER;
struct CONTENT_SERVER
{
    CONTENT_PROVIDER_COMMON content_provider;
    /// Context Store present
    bool bUseContextStore;
    /// Local directory of content currently served
    char localPath[DULINK_MAX_PATH];
    /// Cached copy of DUPKG path
    char dupkgPathCached[DULINK_MAX_PATH];
    /// Last command ID
    uint8_t lastCmdId;
    /// Last command's result
    DU_RCODE lastCmdResult;
    /// Signal piped fd
    int pipeFd[2];
};

/// Global content server object;
extern CONTENT_SERVER content_server;

/* ------------------------ Callbacks --------------------------------------- */
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

/* ------------------------ Public Functions -------------------------------- */
/*!
 * Initialize content server data structure
 *
 * @param[in] pServer
 *      Pointer to uninitialized content server data structure
 */
void contentServerConstruct
(
    PCONTENT_SERVER pServer
);

/*!
 * Export DU Link Nodes and register the plugin
 *
 * @param[in] pServer
 *      Pointer to initialized content server data structure
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

#endif
