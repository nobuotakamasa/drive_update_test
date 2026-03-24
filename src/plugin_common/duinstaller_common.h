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
 * @file  duinstaller_common.h
 * @brief Common definitions and data structures shared between DUv3 Installers
 */

#ifndef DUINSTALLER_COMMON_H_
#define DUINSTALLER_COMMON_H_

/* ------------------------ System Includes --------------------------------- */
#include <stdbool.h>
#include <pthread.h>

/* ------------------------ DU Includes ------------------------------------- */
#include "ducommon.h"
#include "duplugin.h"

/* ------------------------ Defines ----------------------------------------- */
/// maximum command length of installer
#define DUINSTALLER_MAX_CMD_LEN   (512)

/// Directory node of installer last deployed
#define INSTALLER_DIR_LAST_DEPLOYED          "last_deployed"
/// Directory node of installer version
#define INSTALLER_DIR_VER                    "ver"
/// Directory node of installer ver/cmp
#define INSTALLER_DIR_VER_CMP                "ver/cmp"

/// File node of installer plugin type
#define INSTALLER_NODE_PLUGIN_TYPE           NODE_PLUGIN_TYPE
/// File node of installer requested runlevel
#define INSTALLER_NODE_REQUESTED_RL          NODE_REQUESTED_RL
/// File node of installer current runlevel
#define INSTALLER_NODE_CURRENT_RL            NODE_CURRENT_RL
/// File node of installer state
#define INSTALLER_NODE_STATE                 NODE_STATE
/// File node of installer state list
#define INSTALLER_NODE_STATE_LIST            NODE_STATE_LIST
/// File node of installer pending runlevel
#define INSTALLER_NODE_PENDING_RL            NODE_PENDING_RL
/// File node of installer command
#define INSTALLER_NODE_CMD                   NODE_CMD
/// File node of installer command list
#define INSTALLER_NODE_CMD_LIST              NODE_CMD_LIST
/// File node of installer progress
#define INSTALLER_NODE_PROGRESS              NODE_PROGRESS
/// File node of installer persistent ctx path
#define INSTALLER_NODE_PERSISTENT_CTX_PATH   NODE_PERSISTENT_CTX_PATH
/// File node of installer version
#define INSTALLER_NODE_VER_INSTALLER         "ver/installer"
/// File node of installer version comparison
#define INSTALLER_NODE_VER_CMP_INSTALLER_CMP "ver/cmp/installer.cmp"
/// File node of installer version payload
#define INSTALLER_NODE_VER_PAYLOAD           "ver/payload"
/// File node of installer version payload comparison
#define INSTALLER_NODE_VER_CMP_PAYLOAD_CMP   "ver/cmp/payload.cmp"
/// File node of installer last deployed metadata
#define INSTALLER_NODE_LAST_DEPLOYED_MDATA   "last_deployed/metadata"
/// File node of installer last deployed result
#define INSTALLER_NODE_LAST_DEPLOYED_RESULT  "last_deployed/result"

/// Notify Nodes names to be exported in by all installers
/// Installer notify node of current runlevel
#define INSTALLER_NOTIFY_NODE_CURRENT_RL     NOTIFY_NODE_CURRENT_RL
/// Installer notify node of state
#define INSTALLER_NOTIFY_NODE_STATE          NOTIFY_NODE_STATE
/// Installer notify node of pending runlevel
#define INSTALLER_NOTIFY_NODE_PENDING_RL     NOTIFY_NODE_PENDING_RL
/// Installer notify node of progress
#define INSTALLER_NOTIFY_NODE_PROGRESS       NOTIFY_NODE_PROGRESS

/// maximum length of installer version
#define MAX_INSTALLER_VER_LENGTH             (256U)

/* ------------------------ Type Definitions -------------------------------- */
typedef enum INSTALLER_STATE INSTALLER_STATE;
typedef enum INSTALLER_RESULT INSTALLER_RESULT;
typedef struct DU_INSTALLER DU_INSTALLER, *PDU_INSTALLER;

/// Possible states for DU installers
enum INSTALLER_STATE
{
    INSTALLER_STATE_IDLE = 0U,
    INSTALLER_STATE_INSTALLING,
    INSTALLER_STATE_PENDING_RL,
    INSTALLER_STATE_PENDING_RESTART,
    INSTALLER_STATE_PENDING_COMMIT,
    INSTALLER_STATE_REVERTING,
    INSTALLER_STATE_ERROR,
    INSTALLER_STATE_RESTORE_CTX,
    INSTALLER_STATE_MAX
};

/// Possible commands for DU installers
enum INSTALLER_CMD
{
    INSTALLER_CMD_DEPLOY = 0U,
    INSTALLER_CMD_COMMIT,
    INSTALLER_CMD_REVERT,
    INSTALLER_CMD_ABORT,
    INSTALLER_CMD_CLEAR_ERROR,
    INSTALLER_CMD_MAX
};

/// Possible results for DU installers
enum INSTALLER_RESULT
{
    INSTALLER_RESULT_SUCCESS = 0U,
    INSTALLER_RESULT_FAILED,
    INSTALLER_RESULT_ABORTED,
    INSTALLER_RESULT_MAX
};

/// Contains data structures shared by all installers
struct DU_INSTALLER
{
    /// Current state of installer module
    INSTALLER_STATE state;
    ///
    /// String representation of state, must be updated same time as state.
    /// It used to read state as string from DU Link callback
    ///
    char stateStr[DU_STR_SHORT_BUF_SIZE];
    /// Current installation progress from 0 to 100
    uint8_t progress;
    ///
    /// String value of progress, which must be updated at same time as progress.
    /// It used to read progress as string from DU Link callback.
    ///
    char progressStr[4];
    /// String storing last deployed metadata version
    char lastDeployedMetadata[DULINK_MAX_PATH];
    /// String storing last deployed result (SUCCESS/FAILED/ABORTED)
    char lastDeployedResult[DU_STR_SHORT_BUF_SIZE];
    /// String storing last deployed version
    char lastDeployedVersion[MAX_INSTALLER_VER_LENGTH];
    /// Current runlevel of installer
    DU_RUN_LEVEL currentRl;
    /// Pending runlevel of installer
    DU_RUN_LEVEL pendingRl;
    /// Most recently requested rl of installer
    DU_RUN_LEVEL requestedRl;
    /// Mutex for pretecting async access to DU Installer data
    pthread_mutex_t mutex;
    /// Condition variable to wait on for DU Installer flag change
    pthread_cond_t cond;
    /// Flag to signal that a resume is pending (persistent_ctx_path is written)
    bool bResumePending;
    /// Flag to signal that a command is pending (cmd node is written)
    bool bCmdPending;
    /// String storing path to persistent context store
    char persistCtxPath[DULINK_MAX_PATH];
    /// Current command being issued
    char installerCmd[DUINSTALLER_MAX_CMD_LEN];
};

/* ------------------------ Externally Defined Declarations ----------------- */
/// List of viable installer states
extern char * const INSTALLER_STATES_TABLE[INSTALLER_STATE_MAX];

/// List of viable installer commands
extern char * const INSTALLER_CMDS_TABLE[INSTALLER_CMD_MAX];

/// List of viable installer results
extern char * const INSTALLER_RESULTS_TABLE[INSTALLER_RESULT_MAX];

/* ------------------------ Common Installer Functions ---------------------- */
/*!
 * @brief Initialize a DU Installer object to default values
 *
 * @param[in, out] pInstaller(PDU_INSTALLER)
 *      Pointer to DU Installer to initialize
 *
 * @return void
 */
void initDuInstaller
(
    PDU_INSTALLER pInstaller
);

/*!
 * @brief Set progress in the DU Installer. Will set numeric and string representations
 * and then send a notification to .notify listeners if the new value is
 * different than the previous one.
 *
 * @param[in, out] pInstaller(PDU_INSTALLER)
 *      Pointer to DU Installer context
 * @param[in] progress(uint8_t)
 *      Value of progress to set
 *
 * @return DU_OK
 *      Progress value set successfully
 * @return DUCOMMON_ERR_BUFFER_TOO_SMALL
 *      Buffer too small to store progress string
 * @return DUCOMMON_ERR_GENERIC
 *      Error occurred within DU Link during notify
 */
DU_RCODE setInstallerProgress
(
    PDU_INSTALLER pInstaller,
    uint8_t       progress
);

/*!
 * @brief Set current_rl in the DU Installer and send a notification to .notify
 * listeners if the new value is different than the previous one.
 *
 * @param[in, out] pInstaller(PDU_INSTALLER)
 *      Pointer to DU Installer context
 * @param[in] currentRl(DU_RUN_LEVEL)
 *      New currentRl value
 *
 * @return DU_OK
 *      current_rl value set successfully
 * @return DUCOMMON_ERR_GENERIC
 *      Error occurred within DU Link during notify
 */
DU_RCODE setInstallerCurrentRl
(
    PDU_INSTALLER pInstaller,
    DU_RUN_LEVEL  currentRl
);

/*!
 * @brief Set pending_rl in the DU Installer. Will set pending_rl based on new rl
 * requirement and current pending value, and then send a notification to
 * .notify listeners if the new value is different than the previous one.
 *
 *
 * @param[in, out] pInstaller(PDU_INSTALLER)
 *      Pointer to DU Installer context
 * @param[in] pendingRl(DU_RUN_LEVEL)
 *      New pendingRl value
 *
 * @return DU_OK
 *      pending_rl value set successfully
 * @return DUCOMMON_ERR_GENERIC
 *      Error occurred within DU Link during notify
 */
DU_RCODE setInstallerPendingRl
(
    PDU_INSTALLER pInstaller,
    DU_RUN_LEVEL  pendingRl
);

/*!
 * @brief Set state in the DU Installer and send a notification to .notify listeners
 * if the new value is different than the previous one.
 *
 * @param[in, out] pInstaller(PDU_INSTALLER)
 *      Pointer to DU Installer context
 * @param[in] state(INSTALLER_STATE)
 *      New state value
 *
 * @return DU_OK
 *      current_rl value set successfully
 * @return DUCOMMON_ERR_BUFFER_TOO_SMALL
 *      Buffer too small to store state string
 * @return DUCOMMON_ERR_GENERIC
 *      Error occurred within DU Link during notify
 */
DU_RCODE setInstallerState
(
    PDU_INSTALLER   pInstaller,
    INSTALLER_STATE state
);

/*!
 * @brief Set installer info on nodes of last_deployed action. Will be reflected in
 * exported last_deployed/... DU Link nodes
 *
 * @param[in, out] pInstaller(PDU_INSTALLER)
 *      Pointer to DU Installer context
 * @param[in] result(INSTALLER_RESULT)
 *      Result of last deployment
 * @param[in] pMetadataPath(const_char*)
 *      DU Link path of last deployed metadata
 * @param[in] pVer(const_char*)
 *      Version of last deployed package or NULL if not relevant
 *
 * @return DU_OK
 *      current_rl value set successfully
 * @return DUCOMMON_ERR_INVALID_ARGUMENT
 *      Invalid result, invalid version length, or invalid metadata path length
 */
DU_RCODE setInstallerLastDeployedInfo
(
    PDU_INSTALLER     pInstaller,
    INSTALLER_RESULT  result,
    const char       *pMetadataPath,
    const char       *pVer
);

/* ------------------------ Common Installer Callbacks ---------------------- */
/*!
 * @brief Callback for handling of persistent_ctx_path node. It will check if
 * installer in IDLE state and persistCtxPath is empty (make sure we don't
 * resume twice). Then save the path to persistCtxPath and mark resume pending
 * to true. Installer then can check if they want to resume by moving to
 * RESTORING_CTX state or ignore this info.
 *
 * @param[in] pRequestPath(const_char*)
 *          Full path of node being accessed
 * @param[in] pOriginPath(const_char*)
 *          Full path of element originating the request
 * @param[in, out] pCtx(void*)
 *          Pointer to initialized DU_INSTALLER data structure
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
 * @return DULINK_CB_ERR_INVALID_ARGUMENT
 *          Invalid operation or offset
 * @return DULINK_CB_ERR_UNKNOWN
 *          Plugin already configured persistCtxPath or is currently working
 */
DU_RCODE installerPersistCtxCB
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
 * @brief Callback for requested_rl node required for all DU Plugins. Will update
 * current_rl and signal conditional variable
 *
 * @param[in] pRequestPath(const_char*)
 *          Full path of node being accessed
 * @param[in] pOriginPath(const_char*)
 *          Full path of element originating the request
 * @param[in, out] pCtx(void*)
 *          Pointer to initialized DU_INSTALLER data structure
 * @param[in] offset(uint64_t)
 *          Should always be 0, otherwise error
 * @param[in] length(uint64_t)
 *          Length of buffer for write
 * @param[in] pBuf(void*)
 *          Pointer to buffer for write

 * @param[in] operation(DULINK_CB_OPERATION)
 *          IO operation, it should be one of:
 *          - DULINK_CB_WRITE
 *          - DULINK_CB_SIZE
 * @param[out] pRetVal(uint64_t*)
 *          - for operation DULINK_CB_SIZE, 0
 *          - for operation DULINK_CB_WRITE, Actual bytes handled
 *
 * @return DU_OK
 *          Upon success
 * @return DULINK_CB_ERR_INVALID_ARGUMENT
 *          Invalid operation or offset or runlevel requested
 * @return DULINK_CB_ERR_UNKNOWN
 *          Failed to update current_rl
 */
DU_RCODE installerRequestedRlCB
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
 * @brief Callback function invoked when a command is written to installer's cmd node.
 * This will save command written and signal plugin.
 * When this node is read, the most recently issued command will be returned
 *
 * @param[in] pRequestPath(const_char*)
 *          Full path of node being accessed
 * @param[in] pOriginPath(const_char*)
 *          Full path of element originating the request
 * @param[in, out] pCtx(void*)
 *          Pointer to initialized DU_INSTALLER data structure
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
 * @return DULINK_CB_ERR_INVALID_ARGUMENT
 *          Input command not valid or too long
 */
DU_RCODE installerCmdCB
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
#endif
