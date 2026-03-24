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
 * @file  ducontent_provider_common.h
 * @brief Common data structures and definitions for Content provider
 */

#ifndef DU_CONTENT_PROVIDER_COMMON_H_
#define DU_CONTENT_PROVIDER_COMMON_H_

/* ------------------------ System Includes --------------------------------- */
#include <pthread.h>

/* ------------------------ Drive Update Includes --------------------------- */
#include "ducommon.h"
#include "duplugin.h"

/* ------------------------ Defines ----------------------------------------- */
/// Used to tag DU content provider internal error codes.
#define DU_CONTENT_ECODE(err)         DU_ECODE(DU_UID_CONTENT, 0U, (err))

/// Generic content provider error
#define CONTENT_ERR_GENERIC           DU_CONTENT_ECODE(1U)
/// Invalid argument of content provider
#define CONTENT_ERR_INVALID_ARGUMENT  DU_CONTENT_ECODE(2U)
/// Error no found of content provider
#define CONTENT_ERR_NOT_FOUND         DU_CONTENT_ECODE(3U)
/// Buffer too small of content provider
#define CONTENT_ERR_BUFFER_TOO_SMALL  DU_CONTENT_ECODE(4U)

/* ------------------------  Data Structures -------------------------------- */
typedef enum CONTENT_STATE CONTENT_STATE;
typedef enum CONTENT_CMD CONTENT_CMD;

/// Possible states
enum CONTENT_STATE
{
    CONTENT_STATE_INIT = 0U,
    CONTENT_STATE_IDLE,
    CONTENT_STATE_CONFIGURED,
    CONTENT_STATE_MAX
};

/// Table to store possible states
extern char * const CONTENT_STATES_TABLE[CONTENT_STATE_MAX];

/// Possible commands
enum CONTENT_CMD
{
    CMD_SERVE = 0U,
    CMD_STOP_SERVE,
    CONTENT_CMD_MAX
};

/// Table to store possible commands
extern char * const CONTENT_CMDS_TABLE[CONTENT_CMD_MAX];

typedef struct CONTENT_PROVIDER_COMMON CONTENT_PROVIDER_COMMON, *PCONTENT_PROVIDER_COMMON;

/// Common content provider structure
struct CONTENT_PROVIDER_COMMON
{
    /// Mutex lock for shared resources
    pthread_mutex_t mutex;
    /// Condition variable to wait on for flag change
    pthread_cond_t cond;
    /// Current command
    char contentCmd[DU_MAX_CMD_LEN];
    /// Current state
    CONTENT_STATE state;
    /// Current state string
    char stateStr[DU_STR_SHORT_BUF_SIZE];
    /// Current runlevel
    DU_RUN_LEVEL currentRl;
    /// Pending runlevel
    DU_RUN_LEVEL pendingRl;
    /// Most recently requested rl
    DU_RUN_LEVEL requestedRl;
    /// Flag to signal that a resume is pending (persistent_ctx_path is written)
    bool bResumePending;
    /// Path to persistent context node
    char persistCtxPath[DULINK_MAX_PATH];
};

/* ------------------------ Callbacks --------------------------------------- */
/*!
 * @brief Internal call back for requested run level
 *
 * @param[in] pRequestPath(const_char*)
 *          Full path of file being accessed
 *
 * @param[in] pOriginPath(const_char*)
 *          Full path of element originating the request
 *
 * @param[in, out] pCtx(void*)
 *          Pointer to global content provider data structure
 *
 * @param[in] offset(uint64_t)
 *          Offset of IO operation. This parameter would not be valid for
 * DULINK_CB_SIZE operation
 *
 * @param[in] length(uint64_t)
 *          Size wanted to read/write. This parameter would not be valid for
 * DULINK_CB_SIZE operation
 *
 * @param[in] pBuf(void*)
 *          Pointer to buffer provided for read/write operation. This
 * parameter would not be valid for DULINK_CB_SIZE operation
 *
 * @param[in] operation(DULINK_CB_OPERATION)
 *          IO operation, it would be one of:
 *          - DULINK_CB_WRITE
 *          - DULINK_CB_SIZE
 *
 * @param[out] pRetVal(uint64_t*)
 *          - for operation DULINK_CB_WRITE Actual bytes written
 *          - for operation DULINK_CB_SIZE 0
 *
 * @return DU_OK
 *          Upon success
 *
 * @return DULINK_CB_ERR_INVALID_ARGUMENT
 *          Invalid input or content provider state (not hosting files)
 */
DU_RCODE contentRequestedRlCB
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
 * @brief Callback for handling of persistent_ctx_path node. This callback is currently
 * doing nothing
 *
 * @param[in] pRequestPath(const_char*)
 *          Full path of node being accessed
 * @param[in] pOriginPath(const_char*)
 *          Full path of element originating the request
 * @param[in, out] pCtx(void*)
 *          Pointer to global content provider data structure
 * @param[in] offset(uint64_t)
 *          Should always be 0, otherwise error
 * @param[in] length(uint64_t)
 *          Length of buffer for read/write
 * @param[in] pBuf(void*)
 *          Pointer to buffer for read/write
 * @param[in] operation(DULINK_CB_OPERATION)
 *          IO operation, one of:
 *          - DULINK_CB_READ
 *          - DULINK_CB_SIZE
 *          - DULINK_CB_WRITE
 * @param[out] pRetVal(uint64_t*)
 *          - for operation DULINK_CB_READ, Actual bytes read to buffer
 *          - for operation DULINK_CB_SIZE, length of the string
 *          - for operation DULINK_CB_WRITE, Actual bytes handled
 *
 * @return DU_OK
 *          Upon success
 *
 * @return DULINK_CB_ERR_INVALID_ARGUMENT
 *          Invalid input
 *
 * @return DULINK_CB_ERR_UNKNOWN
 *          persistCtxPath already configured or plugin busy
 */
DU_RCODE contentPersistCtxCBPlugin
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
 * @brief Set content provider to a particular state. Mutex should not be held before
 * calling this function
 *
 * @param[in, out] pProvider(PCONTENT_PROVIDER_COMMON)
 *      Pointer to content provider data structure
 *
 * @param[in] state(CONTENT_STATE)
 *      State to be set
*
 * @return void
 */
void contentProviderSetState
(
    PCONTENT_PROVIDER_COMMON pProvider,
    CONTENT_STATE            state
);

/*!
 * @brief Initialize content provider data structures
 *
 * @param[in, out] pProvider(PCONTENT_PROVIDER_COMMON)
 *      Pointer to uninitialized content provider data structure
 *
 * @return void
 */
void initContentProvider
(
    PCONTENT_PROVIDER_COMMON pProvider
);
#endif
