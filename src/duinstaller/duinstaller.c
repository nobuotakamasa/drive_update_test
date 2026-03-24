/*
 * Copyright (c) 2019-2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/*!
 * @file  duinstaller.c
 * @brief State machine and functionality for DU Installer
 */

/* ------------------------ Drive Update Includes --------------------------- */
#include "dutransport.h"
#include "dulink.h"
#include "dulog.h"
#include "utils.h"
#include "duinstaller.h"
#include "duinstaller_helpers.h"
#include "duplugin.h"
#include "plugin_common/duinstaller_common.h"

/* ------------------------ System Includes --------------------------------- */
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>

/* ------------------------ Defines ----------------------------------------- */
#define PLUGIN_DULINK_NAME      "plugin"

/* ------------------------ Forward Declarations ---------------------------- */
// verInstallerCmpCB is referenced in the file-scope node table below
static DU_RCODE verInstallerCmpCB
(const char *pRequestPath, const char *pOriginPath, void *pCtx,
 uint64_t offset, uint64_t length, void *pBuf,
 DULINK_CB_OPERATION operation, uint64_t *pRetVal);

/* ------------------------ Global Data Structures -------------------------- */
// List of commands supported by installer
const char * SAMPLE_CMDS_TABLE[NUM_SAMPLE_CMDS] =
{
    [SAMPLE_CMD_ID_DEPLOY]      = "deploy",
    [SAMPLE_CMD_ID_COMMIT]      = "commit",
    [SAMPLE_CMD_ID_CLEAR_ERROR] = "clear_error"
};

/* ------------------------ DU Link Callback Contexts ----------------------- */
// Contexts for read only string callbacks
static CTX_LOCK_STR ctxPluginType =
{
    .pStr = PLUGIN_TYPE_INSTALLER,
    .pMutex = NULL
};

static CTX_LOCK_STR ctxVerInstaller =
{
    .pStr = SAMPLE_VERSION,
    .pMutex = NULL
};

static CTX_LOCK_STR ctxState =
{
    .pStr = gInstaller.duInstaller.stateStr,
    .pMutex = &gInstaller.duInstaller.mutex
};

static CTX_LOCK_STR ctxProgress =
{
    .pStr = gInstaller.duInstaller.progressStr,
    .pMutex = &gInstaller.duInstaller.mutex
};

static CTX_LOCK_STR ctxLastDeployedMetadata =
{
    .pStr = gInstaller.duInstaller.lastDeployedMetadata,
    .pMutex = &gInstaller.duInstaller.mutex
};

static CTX_LOCK_STR ctxLastDeployedResult =
{
    .pStr = gInstaller.duInstaller.lastDeployedResult,
    .pMutex = &gInstaller.duInstaller.mutex
};

// Contexts for runlevels
static CTX_LOCK_RL ctxCurrentRl =
{
    .pRl = &gInstaller.duInstaller.currentRl,
    .pMutex = &gInstaller.duInstaller.mutex
};

static CTX_LOCK_RL ctxPendingRl =
{
    .pRl = &gInstaller.duInstaller.pendingRl,
    .pMutex = &gInstaller.duInstaller.mutex
};

// Contexts for .list nodes
static CTX_LIST_STR ctxStateList =
{
    .size = INSTALLER_STATE_MAX,
    .ppStrList = (char const **) INSTALLER_STATES_TABLE
};

static CTX_LIST_STR ctxCmdList =
{
    .size = NUM_SAMPLE_CMDS,
    .ppStrList = (char const **) SAMPLE_CMDS_TABLE
};

/* ------------------------ DU Link Node Definitions ------------------------ */
/// Table containing all directory nodes to export
#define INSTALLER_DULINK_DIRS_MAX (3U)
static const DULINK_EXPORT_REQS INSTALLER_DULINK_DIRS_TABLE[INSTALLER_DULINK_DIRS_MAX] =
{
    // last_deployed
    { .pPath = INSTALLER_DIR_LAST_DEPLOYED, .attr = READ_ONLY_DIR_ATTR },
    // ver
    { .pPath = INSTALLER_DIR_VER, .attr = READ_ONLY_DIR_ATTR },
    // ver/cmp
    { .pPath = INSTALLER_DIR_VER_CMP, .attr = READ_ONLY_DIR_ATTR }
};

/// Table containing all file nodes to export
#define INSTALLER_DULINK_FILES_MAX (14U)
static const DULINK_EXPORT_REQS INSTALLER_DULINK_FILES_TABLE[INSTALLER_DULINK_FILES_MAX] =
{
    // plugin-type
    { .pPath = INSTALLER_NODE_PLUGIN_TYPE, .attr = READ_ONLY_ATTR,
      .cb = readOnlyStringCB, .pCtx = &ctxPluginType },
    // requested_rl: use common callback, context is DU_INSTALLER
    { .pPath = INSTALLER_NODE_REQUESTED_RL, .attr = MASTER_ONLY_RW_ATTRIBUTE,
      .cb = installerRequestedRlCB, .pCtx = &gInstaller.duInstaller },
    // current_rl
    { .pPath = INSTALLER_NODE_CURRENT_RL, .attr = READ_ONLY_ATTR,
      .cb = readOnlyRunlevelCB, .pCtx = &ctxCurrentRl },
    // cmd: use common callback, context is DU_INSTALLER
    { .pPath = INSTALLER_NODE_CMD, .attr = READ_WRITE_ATTRIBUTE,
      .cb = installerCmdCB, .pCtx = &gInstaller.duInstaller },
    // cmd.list
    { .pPath = INSTALLER_NODE_CMD_LIST, .attr = READ_ONLY_ATTR,
      .cb = listNodeCB, .pCtx = &ctxCmdList },
    // state
    { .pPath = INSTALLER_NODE_STATE, .attr = READ_ONLY_ATTR,
      .cb = readOnlyStringCB, .pCtx = &ctxState },
    // state.list
    { .pPath = INSTALLER_NODE_STATE_LIST, .attr = READ_ONLY_ATTR,
      .cb = listNodeCB, .pCtx = &ctxStateList },
    // progress
    { .pPath = INSTALLER_NODE_PROGRESS, .attr = READ_ONLY_ATTR,
      .cb = readOnlyStringCB, .pCtx = &ctxProgress },
    // pending_rl
    { .pPath = INSTALLER_NODE_PENDING_RL, .attr = READ_ONLY_ATTR,
      .cb = readOnlyRunlevelCB, .pCtx = &ctxPendingRl },
    // persistent_ctx_path
    { .pPath = INSTALLER_NODE_PERSISTENT_CTX_PATH, .attr = READ_WRITE_ATTRIBUTE,
      .cb = installerPersistCtxCB, .pCtx = &gInstaller.duInstaller },
    // ver/installer
    { .pPath = INSTALLER_NODE_VER_INSTALLER, .attr = READ_ONLY_ATTR,
      .cb = readOnlyStringCB, .pCtx = &ctxVerInstaller },
    // ver/cmp/installer.cmp
    { .pPath = INSTALLER_NODE_VER_CMP_INSTALLER_CMP,
      .attr = READ_WRITE_ATTRIBUTE,
      .cb = verInstallerCmpCB, .pCtx = &gInstaller },
    // last_deployed/metadata
    { .pPath = INSTALLER_NODE_LAST_DEPLOYED_MDATA, .attr = READ_ONLY_ATTR,
      .cb = readOnlyStringCB, .pCtx = &ctxLastDeployedMetadata },
    // last_deployed/result
    { .pPath = INSTALLER_NODE_LAST_DEPLOYED_RESULT, .attr = READ_ONLY_ATTR,
      .cb = readOnlyStringCB, .pCtx = &ctxLastDeployedResult },
};

/// Table containing all .notify nodes to export
#define INSTALLER_DULINK_NOTIFY_MAX (4U)
static const DULINK_EXPORT_REQS INSTALLER_DULINK_NOTIFY_TABLE[INSTALLER_DULINK_NOTIFY_MAX] =
{
    // current_rl.notify
    { .pPath = INSTALLER_NOTIFY_NODE_CURRENT_RL },
    // pending_rl.notify
    { .pPath = INSTALLER_NOTIFY_NODE_PENDING_RL },
    // state.notify
    { .pPath = INSTALLER_NOTIFY_NODE_STATE },
    // progress.notify
    { .pPath = INSTALLER_NOTIFY_NODE_PROGRESS }
};

/* ------------------------ Private Functions ------------------------------- */
// State transition function forward declaration
static DU_RCODE setState
(PSAMPLE_INSTALLER pMyInstaller, INSTALLER_STATE state);

// Version comparison helper for verInstallerCmpCB
static DU_RCODE versionCmp
(
    const char *pV1,
    const char *pV2,
    char       *pResult
)
{
    uint64_t v1Int;
    uint64_t v2Int;
    uint32_t i;
    char    *pEndPtr;

    if (strlen(pV1) == 0 || strlen(pV2) == 0)
    {
        return SAMPLE_ERR_INVALID_ARGUMENT;
    }

    for (i = 0; i < strlen(pV1); i++)
    {
        if (!isdigit(pV1[i]))
        {
            return SAMPLE_ERR_INVALID_ARGUMENT;
        }
    }

    for (i = 0; i < strlen(pV2); i++)
    {
        if (!isdigit(pV2[i]))
        {
            return SAMPLE_ERR_INVALID_ARGUMENT;
        }
    }

    errno = 0;
    v1Int = strtoul(pV1, &pEndPtr, 10);
    if (v1Int == UINT64_MAX || errno != 0)
    {
        return SAMPLE_ERR_INVALID_ARGUMENT;
    }

    errno = 0;
    v2Int = strtoul(pV2, &pEndPtr, 10);
    if (v2Int == UINT64_MAX || errno != 0)
    {
        return SAMPLE_ERR_INVALID_ARGUMENT;
    }

    if (v1Int > v2Int)
    {
        *pResult = '>';
    }
    else if (v1Int < v2Int)
    {
        *pResult = '<';
    }
    else
    {
        *pResult = '=';
    }

    return DU_OK;
}

/*!
 * Callback for ver/cmp/installer.cmp node. Accepts an installer version string,
 * compares it against SAMPLE_VERSION, and stores the result as "v1[<|>|=]v2".
 */
static DU_RCODE verInstallerCmpCB
(
    const char          *pRequestPath,
    const char          *pOriginPath,
    void                *pCtx,
    uint64_t             offset,
    uint64_t             length,
    void                *pBuf,
    DULINK_CB_OPERATION  operation,
    uint64_t            *pRetVal
)
{
    DU_RCODE          duRet = DU_OK;
    PSAMPLE_INSTALLER pSample = (PSAMPLE_INSTALLER) pCtx;
    char              inputVerBuf[MAX_INSTALLER_VER_LENGTH] = {0};
    char              result = '=';
    size_t            inputVerLen;
    size_t            len;

    duMutexLock(&pSample->duInstaller.mutex);
    *pRetVal = 0;

    switch (operation)
    {
        case DULINK_CB_READ:
        {
            if (strlen(pSample->installerCmpBuf) >= length)
            {
                DU_ERR("Not enough space to read version comparison\n");
                duRet = DULINK_CB_ERR_UNKNOWN;
                break;
            }
            strcpy((char *) pBuf, pSample->installerCmpBuf);
            *pRetVal = strlen((char *) pBuf);
            break;
        }
        case DULINK_CB_SIZE:
        {
            *pRetVal = strlen(pSample->installerCmpBuf);
            break;
        }
        case DULINK_CB_WRITE:
        {
            if (length > MAX_INSTALLER_VER_LENGTH)
            {
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                break;
            }
            (void) strncpy(inputVerBuf, (char *) pBuf, length);
            if (versionCmp(inputVerBuf, SAMPLE_VERSION, &result) != DU_OK)
            {
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                DU_ERR("Version compare failed, invalid input\n");
                break;
            }
            inputVerLen = strlen(inputVerBuf);
            // Concatenate v1 [<|>|=] v2
            len = strlcpy(pSample->installerCmpBuf, inputVerBuf,
                ((uint64_t) inputVerLen) + 1U);
            if (len > inputVerLen)
            {
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                DU_ERR("%s is truncated to %lu\n", pSample->installerCmpBuf,
                    len);
                break;
            }

            pSample->installerCmpBuf[inputVerLen] = result;
            (void) strcpy(pSample->installerCmpBuf + inputVerLen + 1,
                SAMPLE_VERSION);
            break;
        }
        default:
        {
            duRet = DULINK_CB_ERR_UNKNOWN;
            break;
        }
    }
    duMutexUnlock(&pSample->duInstaller.mutex);

    return duRet;
}

/*!
 * Run Installer INSTALLER_STATE_IDLE state
 *
 * INSTALLER_STATE_IDLE state is responsible for waiting until a command is sent to the
 * installer, at which point installer wakes up and moves to INSTALLING state
 *
 * @param[in] pMyInstaller
 *      Pointer to installer top level data strucure
 *
 * @return DU_OK
 *      Done waiting, transitioned to INSTALLING state
 * @return SAMPLE_ERR_GENERIC
 *      Transition to INSTALLING state failed
 */
static DU_RCODE installerStateIdle
(
    PSAMPLE_INSTALLER pMyInstaller
)
{
    DU_RCODE duRet = DU_OK;
    bool     needsRL;

    DU_INFO("Installer IDLE state\n");

    // Wait for a command to arrive via installerCmdCB
    duMutexLock(&pMyInstaller->duInstaller.mutex);
    while (!pMyInstaller->duInstaller.bCmdPending)
    {
        duCondWait(&pMyInstaller->duInstaller.cond, &pMyInstaller->duInstaller.mutex);
    }
    // Parse the raw command string stored by installerCmdCB
    duRet = parseCmd(pMyInstaller->duInstaller.installerCmd,
        &pMyInstaller->cmdId,
        pMyInstaller->filePathBuf, (uint32_t) sizeof(pMyInstaller->filePathBuf),
        pMyInstaller->savenameBuf, (uint32_t) sizeof(pMyInstaller->savenameBuf));
    // Capture RL check under lock before releasing
    needsRL = (pMyInstaller->duInstaller.currentRl < RL_BG_APPLY);
    duMutexUnlock(&pMyInstaller->duInstaller.mutex);

    if (duRet != DU_OK)
    {
        DU_ERR("Invalid cmd: parse failed\n");
        duRet = SAMPLE_ERR_INVALID_ARGUMENT;
        goto bailout;
    }

    // Requires RL_BG_APPLY to initiate installing state
    if (!needsRL)
    {
        duRet = setState(pMyInstaller, INSTALLER_STATE_INSTALLING);
    }
    else
    {
        duRet = setInstallerPendingRl(&pMyInstaller->duInstaller, RL_BG_APPLY);
        if (duRet != DU_OK)
        {
            DU_ERR("Error setting or notifying pending RL\n");
            goto bailout;
        }
        duRet = setState(pMyInstaller, INSTALLER_STATE_PENDING_RL);
    }

bailout:
    return duRet;
}

/*!
 * Run Installer ERROR state
 *
 * ERROR state is entered when a command fails asynchronously. ERROR state may
 * be cleared when clear_error command is sent to the intaller, which will
 * return the installer to IDLE state
 *
 * @param[in] pMyInstaller
 *      Pointer to installer top level data strucure
 *
 * @return DU_OK
 *      Done waiting, transitioned to INSTALLER_STATE_IDLE state
 * @return SAMPLE_ERR_INVALID_ARGUMENT
 *      Invalid cmd being executed, reenter error state
 */
static DU_RCODE installerStateError
(
    PSAMPLE_INSTALLER pMyInstaller
)
{
    DU_RCODE duRet = DU_OK;
    int32_t  fd;

    DU_INFO("Installer ERROR state\n");

    // Wait for a command to arrive via installerCmdCB
    duMutexLock(&pMyInstaller->duInstaller.mutex);
    while (!pMyInstaller->duInstaller.bCmdPending)
    {
        DU_INFO("Error occurred: waiting for clear_error cmd\n");
        duCondWait(&pMyInstaller->duInstaller.cond, &pMyInstaller->duInstaller.mutex);
    }
    duRet = parseCmd(pMyInstaller->duInstaller.installerCmd,
        &pMyInstaller->cmdId,
        pMyInstaller->filePathBuf, (uint32_t) sizeof(pMyInstaller->filePathBuf),
        pMyInstaller->savenameBuf, (uint32_t) sizeof(pMyInstaller->savenameBuf));
    duMutexUnlock(&pMyInstaller->duInstaller.mutex);

    if (duRet != DU_OK || pMyInstaller->cmdId != SAMPLE_CMD_ID_CLEAR_ERROR)
    {
        DU_ERR("Error must be cleared before new command can be executed\n");
        return SAMPLE_ERR_INVALID_ARGUMENT;
    }

    DU_INFO("Clearing ERROR state\n");
    // Open staged file and get file descriptor
    fd = open(pMyInstaller->stagedFilePathBuf, O_RDWR);
    // Check if file exists
    if (fd != -1)
    {
        // Delete staged file by file descriptor
        (void) unlinkat(fd, "", 0);
        // Close file descriptor
        (void) close(fd);
    }
    return setState(pMyInstaller, INSTALLER_STATE_IDLE);
}

/*!
 * Run Installer PENDING_COMMIT state
 *
 * PENDING_COMMIT state is responsible for waiting until a commit command
 * is sent to the installer, at which point installer wakes up and moves
 * to INSTALLING state, where it can commit files previously staged by
 * the deploy command
 *
 * @param[in] pMyInstaller
 *      Pointer to installer top level data strucure
 *
 * @return DU_OK
 *      Done waiting, transitioned to INSTALLER_STATE_IDLE state
 * @return SAMPLE_ERR_GENERIC
 *      Transition to another state failed
 */
static DU_RCODE installerStatePendingCommit
(
    PSAMPLE_INSTALLER pMyInstaller
)
{
    DU_RCODE duRet = DU_OK;

    DU_INFO("Installer PENDING_COMMIT state\n");

    // Wait for commit (or unexpected cmd) to arrive via installerCmdCB
    duMutexLock(&pMyInstaller->duInstaller.mutex);
    while (!pMyInstaller->duInstaller.bCmdPending)
    {
        duCondWait(&pMyInstaller->duInstaller.cond, &pMyInstaller->duInstaller.mutex);
    }
    duRet = parseCmd(pMyInstaller->duInstaller.installerCmd,
        &pMyInstaller->cmdId,
        pMyInstaller->filePathBuf, (uint32_t) sizeof(pMyInstaller->filePathBuf),
        pMyInstaller->savenameBuf, (uint32_t) sizeof(pMyInstaller->savenameBuf));
    duMutexUnlock(&pMyInstaller->duInstaller.mutex);

    if (duRet != DU_OK || pMyInstaller->cmdId != SAMPLE_CMD_ID_COMMIT)
    {
        DU_ERR("Expected commit cmd, got %u\n", pMyInstaller->cmdId);
        return SAMPLE_ERR_NOT_SUPPORTED;
    }

    DU_INFO("Committing previous deployment\n");
    if (execCommitCmd(pMyInstaller) != DU_OK)
    {
        DU_ERR("Commit command failed\n");
        return SAMPLE_ERR_GENERIC;
    }
    return setState(pMyInstaller, INSTALLER_STATE_IDLE);
}

/*!
 * Run Installer PENDING_RL state
 *
 * PENDING_RL state is responsible for blocking until installer's runlevel is
 * set high enough to continue update installation
 *
 * @param[in] pMyInstaller
 *      Pointer to installer top level data strucure
 *
 * @return DU_OK
 *      Done waiting, RL set high enough to continue
 * @return SAMPLE_ERR_GENERIC
 *      Transition to INSTALLING state failed
 */
static DU_RCODE installerStatePendingRL
(
    PSAMPLE_INSTALLER pMyInstaller
)
{
    DU_RCODE duRet = DU_OK;
    char     rlBuf[DU_RUNLEVEL_STR_MAX_SIZE];

    DU_INFO("Installer PENDING_RL state\n");

    duMutexLock(&pMyInstaller->duInstaller.mutex);
    while (pMyInstaller->duInstaller.pendingRl < RL_INVALID &&
           pMyInstaller->duInstaller.currentRl <
           pMyInstaller->duInstaller.pendingRl)
    {
        runlevelUintToStr(pMyInstaller->duInstaller.pendingRl,
            rlBuf, sizeof(rlBuf));
        DU_INFO("Waiting on runlevel %s\n", rlBuf);
        duCondWait(&pMyInstaller->duInstaller.cond, &pMyInstaller->duInstaller.mutex);
    }
    duMutexUnlock(&pMyInstaller->duInstaller.mutex);

    // setInstallerPendingRl and setState both lock duInstaller.mutex internally
    duRet = setInstallerPendingRl(&pMyInstaller->duInstaller, RL_INVALID);
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to clear pending rl\n");
        return duRet;
    }
    return setState(pMyInstaller, INSTALLER_STATE_INSTALLING);
}

/*!
 * Run Installer INSTALLING state
 *
 * INSTALLING state is responsible for running an entire update until it either
 * completes or fails. INSTALLING state will transition to IDLE state upon
 * failure or successful completion of an update
 *
 * @param[in] pMyInstaller
 *      Pointer to installer top level data structure
 *
 * @return DU_OK
 *      Update no longer installing, update completed or failed
 * @return SAMPLE_ERR_GENERIC
 *      Unrecoverable error has occurred during installation
 */
static DU_RCODE installerStateInstalling
(
    PSAMPLE_INSTALLER pMyInstaller
)
{
    DU_INFO("Installer INSTALLING state\n");

    // Only deploy can trigger INSTALLING; guard against unexpected state
    if (pMyInstaller->cmdId != SAMPLE_CMD_ID_DEPLOY)
    {
        DU_ERR("Unexpected cmd %u in INSTALLING state\n", pMyInstaller->cmdId);
        return SAMPLE_ERR_NOT_SUPPORTED;
    }

    DU_INFO("Executing deploy cmd\n");
    if (execDeployCmd(pMyInstaller) != DU_OK)
    {
        DU_ERR("Deploy cmd failed\n");
        return SAMPLE_ERR_GENERIC;
    }
    return setState(pMyInstaller, INSTALLER_STATE_PENDING_COMMIT);
}

/*!
 * State transition function, responsible for handling atomic transitions
 * between installer states. Must be called without holding duInstaller.mutex,
 * as the helper functions (setInstallerState, setInstallerLastDeployedInfo)
 * acquire that lock internally.
 *
 * @param[in] pMyInstaller
 *      Pointer to installer top level data structure
 * @param[in] state
 *      Installer state being transitioned to
 *
 * @return DU_OK
 *      Successfully transitioned to new state
 * @return DU_ERR_GENERIC
 *      Unexpected state or transition failed
 *
 */
static DU_RCODE setState
(
    PSAMPLE_INSTALLER pMyInstaller,
    INSTALLER_STATE   state
)
{
    DU_RCODE         duRet = DU_OK;
    INSTALLER_RESULT result = INSTALLER_RESULT_SUCCESS;

    switch (state)
    {
        case INSTALLER_STATE_IDLE:
        {
            if (pMyInstaller->bUpdateFailed)
            {
                DU_ERR("Installation Failed\n");
                result = INSTALLER_RESULT_FAILED;
            }

            if (setInstallerLastDeployedInfo(&pMyInstaller->duInstaller, result,
                pMyInstaller->filePathBuf, NULL) != DU_OK)
            {
                DU_WARN("Failed to set last deployed info\n");
            }

            // Reset command-pending flag and clear stored command
            duMutexLock(&pMyInstaller->duInstaller.mutex);
            memset(pMyInstaller->duInstaller.installerCmd, 0,
                sizeof(pMyInstaller->duInstaller.installerCmd));
            pMyInstaller->duInstaller.bCmdPending = false;
            duMutexUnlock(&pMyInstaller->duInstaller.mutex);

            memset(pMyInstaller->filePathBuf, 0,
                sizeof(pMyInstaller->filePathBuf));
            memset(pMyInstaller->savenameBuf, 0,
                sizeof(pMyInstaller->savenameBuf));
            memset(pMyInstaller->stagedFilePathBuf, 0,
                sizeof(pMyInstaller->stagedFilePathBuf));
            memset(pMyInstaller->installDir, 0,
                sizeof(pMyInstaller->installDir));
            pMyInstaller->bUpdateFailed = false;
            break;
        }
        case INSTALLER_STATE_INSTALLING:
        {
            break;
        }
        case INSTALLER_STATE_PENDING_RL:
        {
            break;
        }
        case INSTALLER_STATE_PENDING_COMMIT:
        {
            // Reset so installerCmdCB can accept the next command (commit)
            duMutexLock(&pMyInstaller->duInstaller.mutex);
            memset(pMyInstaller->duInstaller.installerCmd, 0,
                sizeof(pMyInstaller->duInstaller.installerCmd));
            pMyInstaller->duInstaller.bCmdPending = false;
            duMutexUnlock(&pMyInstaller->duInstaller.mutex);
            break;
        }
        case INSTALLER_STATE_ERROR:
        {
            pMyInstaller->bUpdateFailed = true;
            // Reset so installerCmdCB can accept clear_error
            duMutexLock(&pMyInstaller->duInstaller.mutex);
            memset(pMyInstaller->duInstaller.installerCmd, 0,
                sizeof(pMyInstaller->duInstaller.installerCmd));
            pMyInstaller->duInstaller.bCmdPending = false;
            duMutexUnlock(&pMyInstaller->duInstaller.mutex);
            break;
        }
        default:
        {
            DU_ERR("Unexpected state!\n");
            duRet = SAMPLE_ERR_GENERIC;
            goto bailout;
        }
    }
    if (setInstallerState(&pMyInstaller->duInstaller, state) != DU_OK)
    {
        DU_WARN("Failed to notify listeners on state change\n");
    }

bailout:
    return duRet;
}

/* ------------------------ Public Functions -------------------------------- */
DU_RCODE installerInit
(
    PSAMPLE_INSTALLER pMyInstaller
)
{
    DU_RCODE duRet;

    DULINK_CONNECT_INFO duConnInfo = {0};

    // initDuInstaller initializes duInstaller.mutex and duInstaller.cond
    initDuInstaller(&pMyInstaller->duInstaller);

    // Set installation directory
    if (getcwd(pMyInstaller->installDir, INSTALL_PATH_MAX_LEN) == NULL)
    {
        DU_CRIT("Coult not get local directory path\n");
        duRet = SAMPLE_ERR_GENERIC;
        goto bailout;
    }

    duRet = getPluginConnInfo(PLUGIN_DULINK_NAME, &duConnInfo);
    if (duRet == DUCOMMON_ERR_NOT_FOUND)
    {
        DU_WARN("Can't get plugin info using name %s\n", PLUGIN_DULINK_NAME);
        goto bailout;
    }
    else if (duRet != DU_OK)
    {
        DU_ERR("Failed to get connection info!\n");
        goto bailout;
    }
    else
    {
        DU_DBG("DU_OK on getPluginConnInfo\n");
    }

    duRet = dulinkInit(PLUGIN_DULINK_NAME, duConnInfo.remotePath);
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to init DU Link\n");
        duRet = SAMPLE_ERR_DULINK;
        goto bailout;
    }

    duRet = exportDULinkNodes(INSTALLER_DULINK_DIRS_TABLE,
            INSTALLER_DULINK_DIRS_MAX,
            INSTALLER_DULINK_FILES_TABLE, INSTALLER_DULINK_FILES_MAX,
            INSTALLER_DULINK_NOTIFY_TABLE, INSTALLER_DULINK_NOTIFY_MAX);
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to export all DU Link nodes\n");
        duRet = SAMPLE_ERR_DULINK;
        goto bailout;
    }

    duRet = dulinkOpen(duConnInfo.remotePath, duConnInfo.trType,
            duConnInfo.seType, (PDUTR_TR_PARAM) &duConnInfo.trParamBuf,
            (PDUTR_SEC_PARAM) &duConnInfo.secParamBuf,
            duConnInfo.connType, &duConnInfo.refId);
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to init DU Link open connection %#x\n", duRet);
        duRet = SAMPLE_ERR_DULINK;
        goto bailout;
    }

    duRet = registerToMaster(DUMASTER_PATH);
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to register to DU Master\n");
        duRet = SAMPLE_ERR_GENERIC;
        goto bailout;
    }

    // setState manages duInstaller.mutex internally
    setState(pMyInstaller, INSTALLER_STATE_IDLE);

bailout:
    return duRet;
}

DU_RCODE installerRun
(
    PSAMPLE_INSTALLER pMyInstaller
)
{
    DU_RCODE status;

    for (;;)
    {
        switch (pMyInstaller->duInstaller.state)
        {
            case INSTALLER_STATE_IDLE:
                status = installerStateIdle(pMyInstaller);
                break;
            case INSTALLER_STATE_INSTALLING:
                status = installerStateInstalling(pMyInstaller);
                break;
            case INSTALLER_STATE_PENDING_RL:
                status = installerStatePendingRL(pMyInstaller);
                break;
            case INSTALLER_STATE_PENDING_COMMIT:
                status = installerStatePendingCommit(pMyInstaller);
                break;
            case INSTALLER_STATE_ERROR:
                status = installerStateError(pMyInstaller);
                break;
            default:
                DU_ERR("Unexpected state %d\n", pMyInstaller->duInstaller.state);
                status = SAMPLE_ERR_GENERIC;
                break;
        }

        if (status != DU_OK)
        {
            // setState manages duInstaller.mutex internally
            setState(pMyInstaller, INSTALLER_STATE_ERROR);
        }
    }

    return DU_OK;
}
