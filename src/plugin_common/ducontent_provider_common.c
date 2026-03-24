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
 * @file ducontent_provider_common.c
 * @brief Implementation of common content provider
 */

/* ------------------------ Drive Update Includes --------------------------- */
#include "ducommon.h"
#include "dulink.h"
#include "utils.h"
#include "dulog.h"
#include "plugin_common/ducontent_provider_common.h"

/* ------------------------ Defines ----------------------------------------- */
char * const CONTENT_STATES_TABLE[CONTENT_STATE_MAX] =
{
    [CONTENT_STATE_INIT] = "INIT",
    [CONTENT_STATE_IDLE] = "IDLE",
    [CONTENT_STATE_CONFIGURED] = "CONFIGURED"
};

char * const CONTENT_CMDS_TABLE[CONTENT_CMD_MAX] =
{
    [CMD_SERVE] = "serve",
    [CMD_STOP_SERVE] = "stop_serving"
};

/* ------------------------ DU Link Node Definitions ------------------------ */

/* ------------------------ Global ------------------------------------------ */

/* ------------------------ Functions --------------------------------------- */
void initContentProvider
(
    PCONTENT_PROVIDER_COMMON pProvider
)
{
    (void) pthread_mutex_init(&pProvider->mutex, NULL);
    (void) pthread_cond_init(&pProvider->cond, NULL);
    duMutexLock(&pProvider->mutex);
    pProvider->state = CONTENT_STATE_INIT;
    (void) strlcpy (pProvider->stateStr,
        CONTENT_STATES_TABLE[CONTENT_STATE_INIT], sizeof(pProvider->stateStr));
    pProvider->currentRl = RL_DORMANT;
    pProvider->pendingRl = RL_INVALID;
    pProvider->requestedRl = RL_INVALID;
    (void) memset(pProvider->persistCtxPath, 0,
            sizeof(pProvider->persistCtxPath));
    duMutexUnlock(&pProvider->mutex);
}

void contentProviderSetState
(
    PCONTENT_PROVIDER_COMMON pProvider,
    CONTENT_STATE            state
)
{
    uint64_t index = 0U;

    duMutexLock(&pProvider->mutex);
    if (pProvider->state != state)
    {
        if (!NvConvertLint64toULint64((int64_t) state, &index))
        {
            DU_ERR("Out of range\n");
            NVOS_COV_WHITELIST(deviate, NVOS_MISRA(Directive, 4_9), "SWE-DU-150-SWSADR")
            NVOS_EXIT(-1);
        }
        pProvider->state = state;
        (void) strlcpy(pProvider->stateStr, CONTENT_STATES_TABLE[index],
                       sizeof(pProvider->stateStr));
        (void) dulinkTriggerNotify(NOTIFY_NODE_STATE, pProvider->stateStr,
        sizeof(pProvider->stateStr));
    }
    duMutexUnlock(&pProvider->mutex);
}

/* ------------------------ Callbacks --------------------------------------- */
/*
 * Trace: plugincommonlibrary-contentRequestedRlCB
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
)
{
    DU_RCODE                 duRet = DU_OK;
    PCONTENT_PROVIDER_COMMON pProvider = (PCONTENT_PROVIDER_COMMON) pCtx;
    DU_RUN_LEVEL             rl;
    DU_RUN_LEVEL             oldRl;
    uint64_t                 len;
    char                     tmpBuf[DU_RUNLEVEL_STR_MAX_SIZE] = {0};

    *pRetVal = 0;
    if (offset != 0U)
    {
        DU_ERR("Invalid offset to requested_rl\n");
        duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
        goto bailout;
    }

    duMutexLock(&pProvider->mutex);
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
            oldRl = pProvider->currentRl;
            DU_ASSERT(oldRl < RL_INVALID);
            pProvider->currentRl = rl;
            if (pProvider->currentRl != oldRl)
            {
                len = runlevelUintToStr(rl, tmpBuf, sizeof(tmpBuf));
                (void) dulinkTriggerNotify(NOTIFY_NODE_CURRENT_RL, tmpBuf, len);
            }
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
    duMutexUnlock(&pProvider->mutex);

bailout:
    return duRet;
}

/*
 * Trace: plugincommonlibrary-contentPersistCtxCBPlugin
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
)
{
    DU_RCODE                 duRet = DU_OK;
    PCONTENT_PROVIDER_COMMON pProvider;

    *pRetVal = 0;
    if ((offset != 0U) || (pCtx == NULL) || ((pBuf == NULL) && ((operation == DULINK_CB_READ) || (operation == DULINK_CB_WRITE))))
    {
        DU_ERR("Invalid offset to persist_ctx_path or null input pointer.\n");
        duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
        goto bailout;
    }

    pProvider = (PCONTENT_PROVIDER_COMMON) pCtx;
    duMutexLock(&pProvider->mutex);
    switch (operation)
    {
        case DULINK_CB_READ:
        {
            *pRetVal = strlcpy((char *)pBuf, pProvider->persistCtxPath, length);
            break;
        }
        case DULINK_CB_SIZE:
        {
            *pRetVal = strlen(pProvider->persistCtxPath);
            break;
        }
        case DULINK_CB_WRITE:
        {
            if (length >= DULINK_MAX_PATH)
            {
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                break;
            }
            if ((strcmp(pProvider->persistCtxPath, "") != 0) ||
                (pProvider->state == CONTENT_STATE_CONFIGURED))
            {
                // persistCtxPath already configured or plugin busy
                duRet = DULINK_CB_ERR_UNKNOWN;
                break;
            }
            (void) memcpy((void *)pProvider->persistCtxPath, pBuf, length);
            pProvider->persistCtxPath[length] = '\0';
            *pRetVal = strlen(pProvider->persistCtxPath);
            pProvider->bResumePending = true;
            duCondSignal(&pProvider->cond);
            break;
        }
        default:
        {
            duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
            break;
        }
    }
    duMutexUnlock(&pProvider->mutex);

bailout:
    return duRet;
}
