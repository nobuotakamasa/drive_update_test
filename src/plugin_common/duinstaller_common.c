/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2024, NVIDIA CORPORATION.  All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/*!
 * @file  duinstaller_common.c
 * @brief Implements functionality shared between DUV3 modules
 */

/* ------------------------ Drive Update Includes --------------------------- */
#include "ducommon.h"
#include "dulink.h"
#include "utils.h"
#include "dulog.h"
#include "plugin_common/duinstaller_common.h"

/* ------------------------ Global Definitions ------------------------------ */
char * const INSTALLER_STATES_TABLE[INSTALLER_STATE_MAX] =
{
    [INSTALLER_STATE_IDLE] = "IDLE",
    [INSTALLER_STATE_INSTALLING] = "INSTALLING",
    [INSTALLER_STATE_PENDING_RL] = "PENDING_RL",
    [INSTALLER_STATE_PENDING_COMMIT] = "PENDING_COMMIT",
    [INSTALLER_STATE_PENDING_RESTART] = "PENDING_RESTART",
    [INSTALLER_STATE_REVERTING] = "REVERTING",
    [INSTALLER_STATE_ERROR] = "ERROR",
    [INSTALLER_STATE_RESTORE_CTX] = "RESTORE_CTX",
};

char * const INSTALLER_CMDS_TABLE[INSTALLER_CMD_MAX] =
{
    [INSTALLER_CMD_DEPLOY] = "deploy",
    [INSTALLER_CMD_COMMIT] = "commit",
    [INSTALLER_CMD_REVERT] = "revert",
    [INSTALLER_CMD_ABORT] = "abort",
    [INSTALLER_CMD_CLEAR_ERROR] = "clear_error"
};

char * const INSTALLER_RESULTS_TABLE[INSTALLER_RESULT_MAX] =
{
    [INSTALLER_RESULT_SUCCESS] = "SUCCESS",
    [INSTALLER_RESULT_FAILED] = "FAILED",
    [INSTALLER_RESULT_ABORTED] = "ABORTED"
};

/* ------------------------ Public Functions -------------------------------- */
void initDuInstaller
(
    PDU_INSTALLER pInstaller
)
{
    (void) pthread_mutex_init(&pInstaller->mutex, NULL);
    (void) pthread_cond_init(&pInstaller->cond, NULL);
    duMutexLock(&pInstaller->mutex);
    pInstaller->state = INSTALLER_STATE_IDLE;
    (void) strlcpy(pInstaller->stateStr,
        INSTALLER_STATES_TABLE[INSTALLER_STATE_IDLE],
        sizeof(pInstaller->stateStr));
    pInstaller->progress = 0;
    (void) strlcpy (pInstaller->progressStr, "0", sizeof(pInstaller->progressStr));
    (void) memset(pInstaller->lastDeployedMetadata, 0,
            sizeof(pInstaller->lastDeployedMetadata));
    (void) memset(pInstaller->lastDeployedResult, 0,
            sizeof(pInstaller->lastDeployedResult));
    (void) memset(pInstaller->lastDeployedVersion, 0,
            sizeof(pInstaller->lastDeployedVersion));
    pInstaller->currentRl = RL_DORMANT;
    pInstaller->pendingRl = RL_INVALID;
    pInstaller->requestedRl = RL_INVALID;
    pInstaller->bResumePending = false;
    pInstaller->bCmdPending = false;
    (void) memset(pInstaller->persistCtxPath, 0,
            sizeof(pInstaller->persistCtxPath));
    duMutexUnlock(&pInstaller->mutex);
}

DU_RCODE setInstallerProgress
(
    PDU_INSTALLER pInstaller,
    uint8_t       progress
)
{
    int32_t  ret;
    uint32_t retLen;
    DU_RCODE duRet = DU_OK;
    uint8_t  oldProgress = pInstaller->progress;

    if (progress > 100U)
    {
        duRet = DUCOMMON_ERR_INVALID_ARGUMENT;
        goto bailout;
    }

    duMutexLock(&pInstaller->mutex);
    ret = uint64ToStr((uint64_t) progress, pInstaller->progressStr,
                      sizeof(pInstaller->progressStr), RADIX_DECIMAL, &retLen);
    if (ret != EOK)
    {
        duMutexUnlock(&pInstaller->mutex);
        duRet = DUCOMMON_ERR_BUFFER_TOO_SMALL;
        goto bailout;
    }
    pInstaller->progress = progress;
    duMutexUnlock(&pInstaller->mutex);

    if (progress != oldProgress)
    {
        duRet = dulinkTriggerNotify(NOTIFY_NODE_PROGRESS,
                pInstaller->progressStr, sizeof(pInstaller->progressStr));
        if (duRet != DU_OK)
        {
            duRet = DUCOMMON_ERR_GENERIC;
        }
    }

bailout:
    return duRet;
}

DU_RCODE setInstallerCurrentRl
(
    PDU_INSTALLER pInstaller,
    DU_RUN_LEVEL  currentRl
)
{
    DU_RCODE     duRet = DU_OK;
    uint64_t     len;
    DU_RUN_LEVEL oldRl = pInstaller->currentRl;
    char         runLevelStr[DU_RUNLEVEL_STR_MAX_SIZE] = {0};

    DU_ASSERT(currentRl < RL_INVALID);
    duMutexLock(&pInstaller->mutex);
    pInstaller->currentRl = currentRl;
    duMutexUnlock(&pInstaller->mutex);

    if (pInstaller->currentRl != oldRl)
    {
        len = runlevelUintToStr(currentRl, runLevelStr, sizeof(runLevelStr));
        duRet = dulinkTriggerNotify(NOTIFY_NODE_CURRENT_RL, runLevelStr, len);
        if (duRet != DU_OK)
        {
            duRet = DUCOMMON_ERR_GENERIC;
        }
    }

    return duRet;
}

DU_RCODE setInstallerPendingRl
(
    PDU_INSTALLER pInstaller,
    DU_RUN_LEVEL  pendingRl
)
{
    DU_RCODE     duRet = DU_OK;
    DU_RUN_LEVEL oldPendingRl;
    uint64_t     len;
    char         runLevelStr[DU_RUNLEVEL_STR_MAX_SIZE] = {0};

    duMutexLock(&pInstaller->mutex);
    oldPendingRl = pInstaller->pendingRl;
    // No longer pending any RL
    if (pendingRl >= RL_INVALID)
    {
        pInstaller->pendingRl = RL_INVALID;
    }
    // Set pending RL when no RL was yet pending
    else if ((oldPendingRl >= RL_INVALID) && (pendingRl >= RL_DORMANT))
    {
        pInstaller->pendingRl = pendingRl;
    }
    // Higher rl pending for something, but can continue with smaller increase
    else if (pendingRl <= oldPendingRl)
    {
        pInstaller->pendingRl = pendingRl;
    }
    // Still waiting on lower rl for someting else
    else
    {
        // Do nothing
    }
    duMutexUnlock(&pInstaller->mutex);

    if (pInstaller->pendingRl != oldPendingRl)
    {
        len = runlevelUintToStr(pendingRl, runLevelStr, sizeof(runLevelStr));
        duRet = dulinkTriggerNotify(NOTIFY_NODE_PENDING_RL, runLevelStr, len);
        if (duRet != DU_OK)
        {
            duRet = DUCOMMON_ERR_GENERIC;
        }
    }

    return duRet;
}

DU_RCODE setInstallerState
(
    PDU_INSTALLER   pInstaller,
    INSTALLER_STATE state
)
{
    INSTALLER_STATE oldState;
    DU_RCODE        duRet = DU_OK;
    size_t          len   = 0U;
    uint64_t        index = 0U;

    duMutexLock(&pInstaller->mutex);
    oldState = pInstaller->state;
    pInstaller->state = state;
    if (!NvConvertLint64toULint64((int64_t) state, &index))
    {
        DU_ERR("Out of range\n");
        return DUCOMMON_ERR_INVALID_ARGUMENT;
    }
    len = strlcpy(pInstaller->stateStr, INSTALLER_STATES_TABLE[index],
                  DU_STR_SHORT_BUF_SIZE);
    duMutexUnlock(&pInstaller->mutex);
    if(len >= DU_STR_SHORT_BUF_SIZE)
    {
        DU_ERR("Buffer too small, need %lu at least\n", len);
        duRet = DUCOMMON_ERR_BUFFER_TOO_SMALL;
    }
    else
    {
        if (oldState != pInstaller->state)
        {
            duRet = dulinkTriggerNotify(NOTIFY_NODE_STATE, pInstaller->stateStr,
                sizeof(pInstaller->stateStr));
            if (duRet != DU_OK)
            {
                duRet = DUCOMMON_ERR_GENERIC;
            }
        }
    }

    return duRet;
}

DU_RCODE setInstallerLastDeployedInfo
(
    PDU_INSTALLER     pInstaller,
    INSTALLER_RESULT  result,
    const char       *pMetadataPath,
    const char       *pVer
)
{
    DU_RCODE    duRet = DU_OK;
    const char  defaultVer[] = "0";
    const char *pVerPnt = (pVer != NULL) ? pVer : defaultVer;
    uint64_t    index = 0U;

    duMutexLock(&pInstaller->mutex);
    if ((pMetadataPath == NULL) ||
        (result >= INSTALLER_RESULT_MAX) ||
        (strlen(pMetadataPath) > DU_PATH_MAX) ||
        (strlen(pVerPnt) > MAX_INSTALLER_VER_LENGTH))
    {
        duRet = DUCOMMON_ERR_INVALID_ARGUMENT;
        goto bailout;
    }
    if (!NvConvertLint64toULint64((int64_t) result, &index))
    {
        DU_ERR("Out of range\n");
        duRet = DUCOMMON_ERR_INVALID_ARGUMENT;
        goto bailout;
    }
    (void) strlcpy(pInstaller->lastDeployedResult,
        INSTALLER_RESULTS_TABLE[index],
        sizeof(pInstaller->lastDeployedResult));
    (void) strlcpy(pInstaller->lastDeployedMetadata, pMetadataPath,
        sizeof(pInstaller->lastDeployedMetadata));
    (void) strlcpy(pInstaller->lastDeployedVersion, pVerPnt,
        sizeof(pInstaller->lastDeployedVersion));

bailout:
    duMutexUnlock(&pInstaller->mutex);
    return duRet;
}

/*
 * Trace: plugincommonlibrary-installerPersistCtxCB
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
)
{
    DU_RCODE       duRet                           = DU_OK;
    PDU_INSTALLER  pInstaller;

    *pRetVal = 0;
    if ((offset != 0U) || (pCtx == NULL) || (pBuf == NULL))
    {
        DU_ERR("Invalid offset to callback or null input pointer.\n");
        duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
        goto bailout;
    }

    pInstaller = (PDU_INSTALLER) pCtx;
    duMutexLock(&pInstaller->mutex);
    switch (operation)
    {
        case DULINK_CB_READ:
        {
            *pRetVal = strlcpy((char *)pBuf, pInstaller->persistCtxPath, length);
            break;
        }
        case DULINK_CB_SIZE:
        {
            *pRetVal = strlen(pInstaller->persistCtxPath);
            break;
        }
        case DULINK_CB_WRITE:
        {
            if (length >= DULINK_MAX_PATH)
            {
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                break;
            }
            if ((strcmp(pInstaller->persistCtxPath, "") != 0) ||
                (pInstaller->state != INSTALLER_STATE_IDLE))
            {
                // persistCtxPath already configured or plugin busy
                duRet = DULINK_CB_ERR_UNKNOWN;
                break;
            }
            (void) memcpy((void *)pInstaller->persistCtxPath, pBuf, length);
            pInstaller->persistCtxPath[length] = '\0';
            *pRetVal = strlen(pInstaller->persistCtxPath);
            pInstaller->bResumePending = true;
            duCondSignal(&pInstaller->cond);
            break;
        }
        default:
        {
            duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
            break;
        }
    }
    duMutexUnlock(&pInstaller->mutex);

bailout:
    return duRet;
}

/*
 * Trace: plugincommonlibrary-installerRequestedRlCB
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
)
{
    DU_RCODE      duRet = DU_OK;
    PDU_INSTALLER pInstaller = (PDU_INSTALLER) pCtx;
    char          tmpBuf[DU_RUNLEVEL_STR_MAX_SIZE] = {0};
    DU_RUN_LEVEL  rl;

    *pRetVal = 0;
    if (offset != 0U)
    {
        DU_ERR("Invalid offset to requested_rl\n");
        duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
        goto bailout;
    }

    switch (operation)
    {
        case DULINK_CB_WRITE:
        {
            if (length > DU_RUNLEVEL_STR_MAX_SIZE)
            {
                DU_ERR("Invalid requested_rl\n");
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                break;
            }
            (void) strlcpy(tmpBuf, (const char *)pBuf, length);
            rl = runlevelStrToUint(tmpBuf);
            if (rl == RL_INVALID)
            {
                DU_ERR("Invalid requested_rl %s\n", tmpBuf);
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                break;
            }
            duRet = setInstallerCurrentRl(pInstaller, rl);
            if (duRet != DU_OK)
            {
                DU_ERR("Failed to update current rl");
                duRet = DULINK_CB_ERR_UNKNOWN;
            }
            duCondSignal(&pInstaller->cond);
            *pRetVal = length;
            break;
        }
        case DULINK_CB_SIZE:
        {
            *pRetVal = 0;
            break;
        }
        default:
        {
            duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
            break;
        }
    }

bailout:
    return duRet;
}

/*
 * Trace: plugincommonlibrary-installerCmdCB
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
)
{
    DU_RCODE duRet = DU_OK;
    PDU_INSTALLER pInstaller = (PDU_INSTALLER) pCtx;

    *pRetVal = 0;
    if (offset != 0U)
    {
        DU_ERR("Invalid offset to cmd\n");
        duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
        goto bailout;
    }

    duMutexLock(&pInstaller->mutex);
    switch (operation)
    {
        case DULINK_CB_READ:
        {
            *pRetVal = strlcpy((char *) pBuf, pInstaller->installerCmd, length);
            if (*pRetVal >= length)
            {
                DU_ERR("Not enough space to read cmd\n");
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                *pRetVal = 0;
                break;
            }
            break;
        }
        case DULINK_CB_SIZE:
        {
            *pRetVal = strlen(pInstaller->installerCmd);
            break;
        }
        case DULINK_CB_WRITE:
        {
            if (sizeof(pInstaller->installerCmd) <= length)
            {
                DU_ERR("Command exceeds max length\n");
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                break;
            }
            if (pInstaller->bCmdPending)
            {
                DU_ERR("Installer is pending cmd, please retry later\n");
                duRet = DULINK_CB_ERR_BUSY_RETRY;
                break;
            }
            (void) strlcpy(pInstaller->installerCmd, (char *) pBuf, length);
            pInstaller->installerCmd[length] = '\0';
            // Signal installer
            pInstaller->bCmdPending = true;
            duCondSignal(&pInstaller->cond);
            break;
        }
        default:
        {
            duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
            break;
        }
    }
    duMutexUnlock(&pInstaller->mutex);

bailout:
    return duRet;
}
