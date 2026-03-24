/*
 * Copyright (c) 2019-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/*!
 * @file  content_server.c
 * @brief Implementation of content server
 */

/* ------------------------ Drive Update Includes --------------------------- */
#include "dutransport.h"
#include "dulink.h"
#include "dulog.h"
#include "utils.h"
#include "content_server.h"
#include "ducmd_parser.h"
#include "duplugin.h"

/* ------------------------ System Includes --------------------------------- */
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <signal.h>

/* ------------------------ Defines ----------------------------------------- */
#define MAX_CONTEXT        (DU_1KB)

/* ------------------------ DU Link Node Definitions ------------------------ */
CONTENT_SERVER  content_server;
static uint32_t gRefId = DULINK_CLOSE_ALL;
static sigset_t signal_mask;

static CTX_LOCK_STR ctxPluginType =
{
    .pStr = PLUGIN_TYPE_CONTENT_PROVIDER,
    .pMutex = NULL
};

static CTX_LOCK_STR ctxState =
{
    .pStr = content_server.content_provider.stateStr,
    .pMutex = &content_server.content_provider.mutex
};

// Contexts for runlevels
static CTX_LOCK_RL ctxCurrentRl =
{
    .pRl = &content_server.content_provider.currentRl,
    .pMutex = &content_server.content_provider.mutex
};

static CTX_LOCK_RL ctxPendingRl =
{
    .pRl = &content_server.content_provider.pendingRl,
    .pMutex = &content_server.content_provider.mutex
};

// Contexts for .list nodes
static CTX_LIST_STR ctxStateList =
{
    .size = CONTENT_STATE_MAX,
    .ppStrList = (char const **) CONTENT_STATES_TABLE
};

static CTX_LIST_STR ctxCmdList =
{
    .size = CONTENT_CMD_MAX,
    .ppStrList = (char const **) CONTENT_CMDS_TABLE
};

// Table containing all file nodes to export
static const DULINK_EXPORT_REQS CONTENT_DULINK_FILES_TABLE[] =
{
    // plugin-type
    { .pPath = NODE_PLUGIN_TYPE, .attr = READ_ONLY_ATTR,
      .cb = readOnlyStringCB, .pCtx = &ctxPluginType },
    // requested_rl
    { .pPath = NODE_REQUESTED_RL, .attr = MASTER_ONLY_RW_ATTRIBUTE,
      .cb = contentRequestedRlCB, .pCtx = &content_server.content_provider },
    // current_rl
    { .pPath = NODE_CURRENT_RL, .attr = READ_ONLY_ATTR,
      .cb = readOnlyRunlevelCB, .pCtx = &ctxCurrentRl },
    // cmd
    { .pPath = NODE_CMD, .attr = READ_WRITE_ATTRIBUTE, .cb = cmdCB,
      .pCtx = &content_server },
    // cmd.list
    { .pPath = NODE_CMD_LIST, .attr = READ_ONLY_ATTR, .cb = listNodeCB,
      .pCtx = &ctxCmdList },
    // state
    { .pPath = NODE_STATE, .attr = READ_ONLY_ATTR, .cb = readOnlyStringCB,
      .pCtx = &ctxState },
    // state.list
    { .pPath = NODE_STATE_LIST, .attr = READ_ONLY_ATTR,
      .cb = listNodeCB, .pCtx = &ctxStateList },
    // pending_rl
    { .pPath = NODE_PENDING_RL, .attr = READ_ONLY_ATTR,
      .cb = readOnlyRunlevelCB, .pCtx = &ctxPendingRl },
    // persistent_ctx_path
    { .pPath = NODE_PERSISTENT_CTX_PATH, .attr = READ_WRITE_ATTRIBUTE,
      .cb = contentPersistCtxCBPlugin, .pCtx = &content_server.content_provider },
};

// Table containing all .notify nodes to export
static const DULINK_EXPORT_REQS CONTENT_DULINK_NOTIFY_TABLE[] =
{
    // current_rl.notify
    { .pPath = NOTIFY_NODE_CURRENT_RL },
    // pending_rl.notify
    { .pPath = NOTIFY_NODE_PENDING_RL },
    // state.notify
    { .pPath = NOTIFY_NODE_STATE },
};

/* ------------------------ Prototypes -------------------------------------- */
static DU_RCODE saveDupkgPathToCtxStore(PCONTENT_SERVER pServer, const char *pPath);

/* ------------------------ Global ------------------------------------------ */

/* ------------------------ Functions --------------------------------------- */
/*!
 * Parse command input string into command name and relevant arguments
 *
 * @param[in] pCmdInput
 *      Pointer to raw input command sent to cmd node
 * @param[out] pCmdId
 *      ID of command parsed
 * @param[out] pLocalPath
 *      Output buffer used to store path of local served folder
 * @param[in] localPathLen
 *      Max length of local path buffer
 *
 * @return DU_OK
 *      Command successfully parsed
 * @return CONTENT_ERR_INVALID_ARGUMENT
 *      Invalid command or required fields not found
 * @return CONTENT_ERR_BUFFER_TOO_SMALL
 *      Output buffer is too small
 */
static DU_RCODE parseCmd
(
    const char *pCmdInput,
    uint8_t    *pCmdId,
    char       *pFilePath,
    uint32_t    filePathLen
)
{
    DU_RCODE duRet  = DU_OK;
    char     tokenizeBuf[DU_MAX_CMD_LEN] = {0};
    char     cmdBuf[DU_MAX_CMD_LEN] = {0};
    uint32_t i;
    bool     bFound = false;

    CMD_TOKEN  tokens[MAX_CMD_TOKENS] = {{0}};
    uint32_t   numTokens = 0;
    const char * const pTagLocal = "local_path=";

    // Create a copy for tokenization
    if (strlen(pCmdInput) > DU_MAX_CMD_LEN)
    {
        DU_ERR("Cmd input too long\n");
        return CONTENT_ERR_BUFFER_TOO_SMALL;
    }
    strncpy(tokenizeBuf, pCmdInput, DU_MAX_CMD_LEN);
    tokenizeBuf[DU_MAX_CMD_LEN - 1U] = '\0';

    duRet = parseStringIntoTokens(tokenizeBuf, &numTokens, tokens,
                                  MAX_CMD_TOKENS);
    if (duRet != DU_OK || numTokens == 0)
    {
        DU_ERR("Invalid cmd input\n");
        return CONTENT_ERR_INVALID_ARGUMENT;
    }

    // Match first token to possible commands
    strncpy(cmdBuf, tokenizeBuf, tokens[0].size);
    for (i = 0; i < DU_ARRAY_SIZE(CONTENT_CMDS_TABLE); i++)
    {
        if (strcmp(cmdBuf, CONTENT_CMDS_TABLE[i]) == 0)
        {
            *pCmdId = i;
            bFound = true;
            break;
        }
    }
    if (!bFound)
    {
        DU_ERR("Invalid cmd %s\n", cmdBuf);
        return CONTENT_ERR_INVALID_ARGUMENT;
    }

    // Load additional args for serve cmd
    if (*pCmdId == CMD_SERVE)
    {
        duRet = stripTagFromInput(tokenizeBuf, tokens, numTokens, pTagLocal,
                                  pFilePath, filePathLen);
        if (duRet != DU_OK)
        {
            DU_ERR("Could not load local serve path!\n");
            return CONTENT_ERR_INVALID_ARGUMENT;
        }
    }

    return DU_OK;
}

void contentServerConstruct(PCONTENT_SERVER pServer)
{
    initContentProvider(&pServer->content_provider);
    (void)memset(pServer->localPath, 0, sizeof(pServer->localPath));
    (void)memset(pServer->dupkgPathCached, 0, sizeof(pServer->dupkgPathCached));
    pServer->bUseContextStore = false;
    pServer->lastCmdId = 0U;
    pServer->lastCmdResult = DU_OK;
}

// Wait for context store during start-up
static void waitForContextStore(PCONTENT_SERVER pServer)
{
    int32_t ret;
    int32_t maxTries = 5;
    duMutexLock(&pServer->content_provider.mutex);
    while (!pServer->content_provider.bResumePending)
    {
        ret = duCondWaitTimed(&pServer->content_provider.cond,
            &pServer->content_provider.mutex, 1000);
        if (--maxTries == 0 || ret == -1)
        {
            DU_WARN("Failed to get ctx store path - is it running?\n");
            break;
        }
    }
    // Context restoring
    if (pServer->content_provider.bResumePending)
    {
        pServer->content_provider.bResumePending = false;
        if (strcmp(pServer->content_provider.persistCtxPath, "") != 0)
        {
            DU_DBG("ctx_path: %s\n", pServer->content_provider.persistCtxPath);
            pServer->bUseContextStore = true;
        }
        else
        {
            DU_WARN("empty ctx_path\n");
            pServer->bUseContextStore = false;
        }
    }
    duMutexUnlock(&pServer->content_provider.mutex);
}

DU_RCODE contentServerInit
(
    PCONTENT_SERVER pServer
)
{
    DU_RCODE duRet;
    DULINK_CONNECT_INFO duConnInfo = {0};

    if (pipe(pServer->pipeFd) == -1)
    {
        DU_ERR("Failed to create pipe for signal monitor\n");
        duRet = CONTENT_ERR_GENERIC;
        goto bailout;
    }
    if (getPluginConnInfo(CONTENT_SERVER_NAME, &duConnInfo) != DU_OK)
    {
        DU_ERR("Failed to get connection info!\n");
        duRet = CONTENT_ERR_INVALID_ARGUMENT;
        goto bailout;
    }

    duRet = dulinkInit(CONTENT_SERVER_NAME, duConnInfo.remotePath);
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to init DU Link\n");
        duRet = CONTENT_ERR_INVALID_ARGUMENT;
        goto bailout;
    }

    duRet = exportDULinkNodes(NULL, 0,
            CONTENT_DULINK_FILES_TABLE,
            DU_ARRAY_SIZE(CONTENT_DULINK_FILES_TABLE),
            CONTENT_DULINK_NOTIFY_TABLE,
            DU_ARRAY_SIZE(CONTENT_DULINK_NOTIFY_TABLE));
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to export all DU Link nodes\n");
        duRet = CONTENT_ERR_INVALID_ARGUMENT;
        goto bailout;
    }

    duRet = dulinkOpen(duConnInfo.remotePath, duConnInfo.trType,
            duConnInfo.seType, (PDUTR_TR_PARAM) &duConnInfo.trParamBuf,
            (PDUTR_SEC_PARAM) &duConnInfo.secParamBuf,
            duConnInfo.connType, &gRefId);
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to open DU Link connection\n");
        duRet = CONTENT_ERR_INVALID_ARGUMENT;
        goto bailout;
    }
    duRet = registerToMaster(DUMASTER_PATH);
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to register to DU Master\n");
        duRet = CONTENT_ERR_INVALID_ARGUMENT;
        goto bailout;
    }
    waitForContextStore(&content_server);
    contentProviderSetState(&pServer->content_provider, CONTENT_STATE_IDLE);

bailout:
    return duRet;
}

/*!
 * Load file list within DUPKG and tokenize as json
 *
 * @param[in] pRootPath
 *      Pointer to local DUPKG path
 *
 * @param[out] pFileBuf
 *      Pointer to buffer to load json content
 *
 * @param[in] fileBufSize
 *      Size of file buffer
 *
 * @param[out] ppJsonRootElmt
 *      Pointer to pointer of json element
 *
 * @param[out] pElementArray
 *      Pointer to json element array to be filled
 *
 * @param[in] elementArraySize
 *      Size of json element array
 *
 * @return DU_OK
 *      Successfully load and tokenize file list
 *
 * @return CONTENT_ERR_GENERIC
 *      Error happened
 */
static DU_RCODE loadFileListAndTokenize
(
    const char     *pRootPath,
    char           *pFileBuf,
    uint32_t        fileBufSize,
    jsonElement_t **ppJsonRootElmt,
    char           *pElementArray,
    uint32_t        elementArraySize
)
{
    DU_RCODE     duRet = DU_OK;
    char         localPath[DU_PATH_MAX];
    FILE        *pFileList;
    json_t      *pJson = NULL;
    size_t       numBytes;

    if (duPathJoin(pRootPath, FILE_LIST_NAME, localPath,
                   sizeof(localPath)) == NULL)
    {
        DU_ERR("Failed to construct list_of_files path\n");
        return CONTENT_ERR_BUFFER_TOO_SMALL;
    }

    // Load list_of_files into local buf
    pFileList = fopen(localPath, "r");
    if (pFileList == NULL)
    {
        DU_ERR("Failed to open list_of_files\n");
        return CONTENT_ERR_NOT_FOUND;
    }

    numBytes = fread(pFileBuf, 1, fileBufSize, pFileList);
    if (numBytes >= fileBufSize)
    {
        DU_ERR("File list is too large to fit in buffer\n");
        (void)fclose(pFileList);
        return CONTENT_ERR_BUFFER_TOO_SMALL;
    }
    if (ferror(pFileList))
    {
        DU_ERR("Error occurred while reading file list\n");
        (void)fclose(pFileList);
        return CONTENT_ERR_GENERIC;
    }
    (void)fclose(pFileList);

    // Parse JSON file
    pJson = jsonParser(pFileBuf, fileBufSize, pElementArray, elementArraySize);
    if (pJson == NULL)
    {
        DU_ERR("Could not parse JSON\n");
        return CONTENT_ERR_INVALID_ARGUMENT;
    }
    // Get root element
    *ppJsonRootElmt = jsonGetRoot(pJson);
    if (*ppJsonRootElmt == NULL)
    {
        DU_ERR("Could not get root element\n");
        return CONTENT_ERR_INVALID_ARGUMENT;
    }

    if (jsonGetType(*ppJsonRootElmt) != JSON_ARRAY)
    {
        DU_ERR("Illegal json format\n");
        return CONTENT_ERR_INVALID_ARGUMENT;
    }

    return duRet;
}

/*!
 * Export or unlink a fileList entry to/from DULINK space
 *
 * @param[in] pServer
 *      Pointer to content server data structure
 *
 * @param[in] pRootPath
 *      Pointer to local DUPKG path
 *
 * @param[in] pJsonElmt
 *      Pointer to json element
 *
 * @param[in] bOperation
 *      Set true to export entry and set false to unlink entry
 *
 * @return DU_OK
 *      Successfully export or unlink the entry
 *
 * @return CONTENT_ERR_GENERIC
 *      Error happened
 */
static DU_RCODE entryOperation
(
    PCONTENT_SERVER  pServer,
    const char      *pRootPath,
    jsonElement_t   *pJsonElmt,
    bool             bOperation
)
{
    DU_RCODE     duRet = DU_OK;
    char         localPath[DU_PATH_MAX];
    char         localName[DU_PATH_MAX];
    char         localType[DU_STR_SHORT_BUF_SIZE];
    char        *pFilename;
    char         nodePath[DULINK_MAX_PATH];
    DULINK_ATTR  dirAttr = READ_ONLY_DIR_ATTR;
    DULINK_ATTR  fileAttr = READ_ONLY_ATTR;
    uint64_t      jsonLen = 0U;

    jsonLen = jsonGetStrByName(pJsonElmt, "name", localName, sizeof(localName));
    if (jsonLen == 0U)
    {
        DU_ERR("Could load local name\n");
        return CONTENT_ERR_INVALID_ARGUMENT;
    }
    else if (jsonLen >= sizeof(localName))
    {
        DU_ERR("Buffer is too small to get %s.\n", "name");
        return DUCOMMON_ERR_BUFFER_TOO_SMALL;
    }
    else
    {
        // Nothing to do here.
    }

    jsonLen = jsonGetStrByName(pJsonElmt, "type", localType, sizeof(localType));
    if (jsonLen == 0U)
    {
        DU_ERR("Could load file type\n");
        return CONTENT_ERR_INVALID_ARGUMENT;
    }
    else if (jsonLen >= sizeof(localType))
    {
        DU_ERR("Buffer is too small to get %s.\n", "type");
        return DUCOMMON_ERR_BUFFER_TOO_SMALL;
    }
    else
    {
        // Nothing to do here.
    }

    // Construct node path
    (void)strcpy(nodePath, CONTENT_FILE_PATH);
    // Skip ./ prefix
    if ((localName[0] == '.') && (localName[1] == '/'))
    {
        pFilename = &localName[2];
    }
    else
    {
        pFilename = localName;
    }
    if (*pFilename != '\0')
    {
        (void)strcat(nodePath, "/");
        (void)strcat(nodePath, pFilename);
    }
    // Check if file can be accessed first when register file
    if (bOperation)
    {
        (void)strlcpy(localPath, pRootPath, sizeof(localPath));
        (void)duPathJoin(pRootPath, pFilename, localPath, sizeof(localPath));
        if (access(localPath, R_OK) != 0)
        {
            DU_ERR("Failed to access local file %s\n", localPath);
            return CONTENT_ERR_GENERIC;
        }
    }
    if (strcmp(localType, "dir") == 0)
    {
        if (bOperation)
        {
            duRet = dulinkExportDirectory(nodePath, &dirAttr);
            if (duRet != DU_OK)
            {
                DU_ERR("Failed to export folder %s\n", nodePath);
                return CONTENT_ERR_GENERIC;
            }
        }
        else
        {
            duRet = dulinkUnlinkDirectory(nodePath);
            if (duRet != DU_OK)
            {
                DU_ERR("Failed to unlink folder %s\n", nodePath);
                return CONTENT_ERR_GENERIC;
            }
        }
    }
    else if (strcmp(localType, "file") == 0)
    {
        if (bOperation)
        {
            duRet = dulinkExportFile(nodePath, &fileAttr,
                                    exportFileCB, pServer);
            if (duRet != DU_OK)
            {
                DU_ERR("Failed to export file %s\n", nodePath);
                return CONTENT_ERR_GENERIC;
            }
        }
        else
        {
            duRet = dulinkUnlinkFile(nodePath);
            if (duRet != DU_OK)
            {
                DU_ERR("Failed to unlink file %s\n", nodePath);
                return CONTENT_ERR_GENERIC;
            }
        }
    }
    else
    {
        DU_ERR("Invalid file type\n");
        return CONTENT_ERR_INVALID_ARGUMENT;
    }

    return duRet;
}

/*!
 * Map local folder specified by pRootPath to DU LINK space /content/files
 * according to list_of_files.json
 *
 * @param[in] pServer
 *      Pointer to content server data structure
 *
 * @param[in] pRootPath
 *      Pointer to local DUPKG path
 *
 * @return DU_OK
 *      Successfully exported every file
 * @return CONTENT_ERR_GENERIC
 *      Error happened
 */
static DU_RCODE serveFolder
(
    PCONTENT_SERVER  pServer,
    const char      *pRootPath
)
{
    DU_RCODE       duRet = DU_OK;
    char           fileBuf[FILE_LIST_MAX_SIZE] = {0};
    char           elementArray[DU_1KB * 32];
    jsonElement_t *pJsonRootElmt = NULL;
    jsonElement_t *pJsonTmpElmt = NULL;
    uint32_t       jsonArraySize = 0U;
    uint32_t       jsonIndex;

    duRet = loadFileListAndTokenize(pRootPath, fileBuf, sizeof(fileBuf),
                                    &pJsonRootElmt, elementArray,
                                    sizeof(elementArray));
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to load or parse JSON\n");
        return duRet;
    }

    jsonArraySize = jsonGetArraySize(pJsonRootElmt);
    // Iterate over entry array
    for (jsonIndex = 0U; jsonIndex < jsonArraySize; jsonIndex++)
    {
        pJsonTmpElmt = jsonGetByIndex(pJsonRootElmt, jsonIndex);
        if (pJsonTmpElmt == NULL)
        {
            DU_ERR("Error on get json by index %d\n", jsonIndex);
            return CONTENT_ERR_INVALID_ARGUMENT;
        }

        duRet = entryOperation(pServer, pRootPath, pJsonTmpElmt, true);
        if (duRet != DU_OK)
        {
            DU_ERR("Failed to register entry\n");
            return CONTENT_ERR_GENERIC;
        }
    }
    return duRet;
}

/*!
 * Clear exported folder in DU LINK space and unlink /content/files according
 * to list_of_files.json
 *
 * @param[in] pRootPath
 *      Pointer to local DUPKG path
 *
 * @return DU_OK
 *      Successfully unlinked every file from DU LINK
 * @return CONTENT_ERR_GENERIC
 *      Error happened
 */
static DU_RCODE stopServe
(
    const char *pRootPath
)
{
    DU_RCODE       duRet = DU_OK;
    char           fileBuf[FILE_LIST_MAX_SIZE] = {0};
    char           elementArray[DU_1KB * 32];
    jsonElement_t *pJsonRootElmt = NULL;
    jsonElement_t *pJsonTmpElmt = NULL;
    uint32_t       jsonArraySize = 0U;
    uint32_t       jsonIndex;

    duRet = loadFileListAndTokenize(pRootPath, fileBuf, sizeof(fileBuf),
                                    &pJsonRootElmt, elementArray,
                                    sizeof(elementArray));
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to load or parse JSON\n");
        return duRet;
    }

    jsonArraySize = jsonGetArraySize(pJsonRootElmt);
    // Iterate over entry array in reverse under the assumption that files/dirs
    // were created in order previously.
    for (jsonIndex = jsonArraySize - 1U;; jsonIndex--)
    {
        pJsonTmpElmt = jsonGetByIndex(pJsonRootElmt, jsonIndex);
        if (pJsonTmpElmt == NULL)
        {
            DU_ERR("Error on get json by index %d\n", jsonIndex);
            return CONTENT_ERR_INVALID_ARGUMENT;
        }
        duRet = entryOperation(NULL, pRootPath, pJsonTmpElmt, false);
        if (duRet != DU_OK)
        {
            DU_DBG("Failed to unlink entry\n");
            // Ignore error and continue to clean up
        }
        if (jsonIndex == 0U)
        {
            break;
        }
    }

    return duRet;
}

/*!
 * Executed command depends on cmdId recoreded in content server data structure.
 * Lock shall be held for this function.
 *
 * @param[in,out] pServer
 *      Pointer to content server data structure
 * @param[in] cmdId
 *      Command ID to be executed
 * @param[in] pLocalPath
 *      Pointer to local serving path
 *
 * @return DU_OK
 *      Successfully linked or unlinked every file from DU LINK
 * @return CONTENT_ERR_INVALID_ARGUMENT
 *      Error happened
 */
static DU_RCODE executeCmd
(
    PCONTENT_SERVER  pServer,
    uint8_t          cmdId,
    const char       *pLocalPath
)
{
    pServer->lastCmdId = cmdId;
    pServer->lastCmdResult = CONTENT_ERR_INVALID_ARGUMENT;
    switch (cmdId)
    {
        case CMD_SERVE:
            if (pServer->content_provider.state == CONTENT_STATE_CONFIGURED)
            {
                DU_LOG("Files are already being served (%s)\n", pServer->localPath);
                return CONTENT_ERR_INVALID_ARGUMENT;
            }
            if (serveFolder(pServer, pLocalPath) != DU_OK)
            {
                DU_ERR("Failed to serve '%s'\n", pLocalPath);
                return CONTENT_ERR_INVALID_ARGUMENT;
            }
            duMutexUnlock(&pServer->content_provider.mutex);
            contentProviderSetState(&pServer->content_provider, CONTENT_STATE_CONFIGURED);
            duMutexLock(&pServer->content_provider.mutex);
            (void)strlcpy(pServer->localPath, pLocalPath, sizeof(pServer->localPath));
            DU_LOG("Files are successfully exported to dulink\n");
            break;
        case CMD_STOP_SERVE:
            if (pServer->content_provider.state != CONTENT_STATE_CONFIGURED)
            {
                DU_ERR("Not currently serving files!\n");
                return CONTENT_ERR_INVALID_ARGUMENT;
            }
            if (stopServe(pServer->localPath) != DU_OK)
            {
                DU_ERR("Failed to stop serve '%s' cleanly!\n", pServer->localPath);
                return CONTENT_ERR_INVALID_ARGUMENT;
            }
            duMutexUnlock(&pServer->content_provider.mutex);
            contentProviderSetState(&pServer->content_provider, CONTENT_STATE_IDLE);
            duMutexLock(&pServer->content_provider.mutex);
            (void)memset(pServer->localPath, 0, sizeof(pServer->localPath));
            break;
        default:
            return CONTENT_ERR_INVALID_ARGUMENT;
    }

    (void)saveDupkgPathToCtxStore(pServer, pServer->localPath);
    pServer->lastCmdResult = DU_OK;
    return DU_OK;
}

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
)
{
    DU_RCODE          duRet = DU_OK;
    PCONTENT_SERVER   pServer = (PCONTENT_SERVER) pCtx;
    char              tmpCmd[DU_MAX_CMD_LEN];
    char              tmpPath[DULINK_MAX_PATH];
    uint8_t           cmdId;

    *pRetVal = 0;

    if (offset != 0)
    {
        DU_ERR("Invalid offset to cmd\n");
        duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
        goto bailout;
    }

    duMutexLock(&pServer->content_provider.mutex);
    switch (operation)
    {
        case DULINK_CB_READ:
        {
            if (length < strlen(pServer->content_provider.contentCmd))
            {
                DU_ERR("Not enough space to read cmd\n");
                duRet = DULINK_CB_ERR_UNKNOWN;
                break;
            }
            strncpy((char *) pBuf, pServer->content_provider.contentCmd, length);
            *pRetVal = strlen((char *) pBuf);
            break;
        }
        case DULINK_CB_SIZE:
        {
            *pRetVal = strlen(pServer->content_provider.contentCmd);
            break;
        }
        case DULINK_CB_WRITE:
        {
            if (sizeof(tmpCmd) <= length)
            {
                DU_ERR("Command exceeds max length\n");
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                break;
            }
            strncpy(tmpCmd, (char *) pBuf, length);
            tmpCmd[length] = '\0';

            duRet = parseCmd(tmpCmd, &cmdId, tmpPath, sizeof(tmpPath));
            if (duRet != DU_OK)
            {
                DU_ERR("Invalid cmd %s\n", tmpCmd);
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                break;
            }
            duRet = executeCmd(pServer, cmdId, tmpPath);
            if (duRet != DU_OK)
            {
                DU_ERR("Failed to execute cmd %s\n", tmpCmd);
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                break;
            }
            (void)strcpy(pServer->content_provider.contentCmd, tmpCmd);
            break;
        }
        default:
        {
            duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
            break;
        }
    }
    duMutexUnlock(&pServer->content_provider.mutex);

bailout:
    return duRet;
}

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
)
{
    DU_RCODE          ret = DU_OK;
    PCONTENT_SERVER   pServer = (PCONTENT_SERVER) pCtx;

    char              tmpPath[DULINK_MAX_PATH];
    char              contentPath[DULINK_MAX_PATH];
    FILE             *pFile;
    struct stat       fileStat;

    *pRetVal = 0;

    if (pRequestPath == NULL)
    {
        DU_ERR("Request path is NULL\n");
        ret = DULINK_CB_ERR_INVALID_ARGUMENT;
        goto ret;
    }

    duMutexLock(&pServer->content_provider.mutex);
    if (pServer->content_provider.state != CONTENT_STATE_CONFIGURED)
    {
        DU_ERR("Not currently serving the file!\n");
        ret = DULINK_CB_ERR_INVALID_ARGUMENT;
        duMutexUnlock(&pServer->content_provider.mutex);
        goto ret;
    }
    // Skip common prefix
    (void)strlcpy(tmpPath, pRequestPath+strlen(CONTENT_FILE_FULL_PATH), sizeof(tmpPath));
    (void)duPathJoin(pServer->localPath, tmpPath,
                      contentPath, sizeof(contentPath));
    duMutexUnlock(&pServer->content_provider.mutex);

    switch (operation)
    {
        case DULINK_CB_READ:
            pFile = fopen(contentPath, "rb");
            if (pFile == NULL)
            {
                DU_ERR("Failed to open file %s\n", strerror(errno));
                ret = DULINK_CB_ERR_IO;
                goto ret;
            }
            if (offset > (uint64_t)LONG_MAX)
            {
                DU_ERR("Offset is too large\n");
                ret = DULINK_CB_ERR_IO;
                (void)fclose(pFile);
                goto ret;
            }
            if (fseek(pFile, offset, SEEK_SET) != 0)
            {
                DU_ERR("Failed to seek inside file %s\n", strerror(errno));
                ret = DULINK_CB_ERR_IO;
                (void)fclose(pFile);
                goto ret;
            }
            *pRetVal = fread(pBuf, 1, length, pFile);
            if (ferror(pFile))
            {
                DU_ERR("Error occurred while reading\n");
                ret = DULINK_CB_ERR_IO;
                (void)fclose(pFile);
                goto ret;
            }
            (void)fclose(pFile);
            break;

        case DULINK_CB_SIZE:
            if (stat(contentPath, &fileStat) != 0)
            {
                DU_ERR("Failed to obtain stat of file %s\n", strerror(errno));
                ret = DULINK_CB_ERR_IO;
                goto ret;
            }
            if (fileStat.st_size < 0)
            {
                DU_ERR("The value %ld of fileStat.st_size is less than 0 \n", fileStat.st_size);
                ret = DULINK_CB_ERR_IO;
                goto ret;
            }
            *pRetVal = fileStat.st_size;
            break;

        case DULINK_CB_WRITE:
            pFile = fopen(contentPath, "r+b");
            if (pFile == NULL)
            {
                DU_ERR("Failed to open file %s\n", strerror(errno));
                ret = DULINK_CB_ERR_IO;
                goto ret;
            }
             if (offset > (uint64_t)LONG_MAX)
            {
                DU_ERR("Offset is too large\n");
                ret = DULINK_CB_ERR_IO;
                (void)fclose(pFile);
                goto ret;
            }
            if (fseek(pFile, offset, SEEK_SET) != 0)
            {
                DU_ERR("Failed to seek inside file %s\n", strerror(errno));
                ret = DULINK_CB_ERR_IO;
                (void)fclose(pFile);
                goto ret;
            }
            *pRetVal = fwrite(pBuf, 1, length, pFile);
            if (*pRetVal == 0U)
            {
                DU_ERR("Failed to write the expected number of bytes,"
                       "length:%lu, pRetVal:%lu\n", length, *pRetVal);
                ret = DULINK_CB_ERR_IO;
                (void)fclose(pFile);
                goto ret;
            }
            if (ferror(pFile))
            {
                DU_ERR("Error occurred while writing\n");
                ret = DULINK_CB_ERR_IO;
                (void)fclose(pFile);
                goto ret;
            }
           (void)fclose(pFile);
            break;

        default:
            ret = DULINK_CB_ERR_INVALID_ARGUMENT;
            break;
    }

ret:
    return ret;
}

static void usage(void)
{
    (void)printf("Usage: content_server <dupkg path>\n");
    (void)printf("       content_server [<option>]\n");
    (void)printf("\n");
    (void)printf("Content server serves files in dupkg located by <dupkg path>\n");
    (void)printf("If <dupkg path> is not specified, it attempts to serve dupkg that"
                 " was previously used (-r option).\n");
    (void)printf("\n");
    (void)printf("Options:\n");
    (void)printf("  -h|--help      Prints this message\n");
    (void)printf("  -c|--clear|-a  Clears the dupkg path that may be saved in context store and exits\n");
    (void)printf("  -i|--idle      Start content server in idle mode\n");
    (void)printf("  -r|--resume    Serving files using the dupkg path previous used if it exists\n");
    (void)printf("                 Not specifying <dupkg path> is equivalent to setting this option\n");

}

static void deregisterContentServer(void)
{
    char        cmdBuf[DU_STR_SHORT_BUF_SIZE] = {0};
    const char *pPath = DUMASTER_PATH "/plugins";
    uint64_t    retLen;
    uint64_t    result = 0UL;

    (void)strncpy(cmdBuf, "deregister", DU_STR_SHORT_BUF_SIZE);
    if (AddU64((uint64_t)strlen(cmdBuf), 1U, &result))
    {
        if (dulinkWrite(pPath, 0U, result, cmdBuf, &retLen) != DU_OK)
        {
            DU_ERR("Can't deregister from master\n");
        }
    }
    else
    {
        DU_ERR("Buffer exceeds the maximum length, strlen(cmdBuf)+1U:%lu\n", result);
    }
}

static void cleanUp(void)
{
    deregisterContentServer();

    if (dulinkClose(gRefId) != DU_OK)
    {
        DU_ERR("Error disconnect DU LINK\n");
    }
}

static void* thread_SignalMon(void *args)
{
    int rc;
    int signum;
    uint8_t dummy = 0U;
    PCONTENT_SERVER  pServer = (PCONTENT_SERVER)args;

    rc = sigwait(&signal_mask, &signum);
    if (rc != 0) {
        DU_ERR("Error sigwait\n");
    }
    if (signum != SIGINT && signum != SIGTERM) {
        DU_WARN("Unexpected signal (%d) received. Exiting anyway...\n", signum);
    }

    // Signal the CLI thread by simply closing the fd.
    if(write(pServer->pipeFd[1], &dummy, 1) != 1)
    {
        // Ignore the error
    }
    (void)close(pServer->pipeFd[1]);
    (void)sleep(5);
    // If we get here, we're stuck somewhere and we should quit anyways.
    (void)printf("\nNot responding. Killing content server forcefully\n");
    cleanUp();
    exit(2);
    return NULL;
}

/* ------------------------ Operation Mode ---------------------------------- */
typedef enum CLI_OP_MODE
{
    INIT = 0U,
    EXIT_CLEAR_CTX,
    SERVE_RESTORE,
    SERVE_NEW,
    SERVE_IDLE,
} CLI_OP_MODE;

static CLI_OP_MODE setOpMode
(
    CLI_OP_MODE curOpmode,
    CLI_OP_MODE newOpmode,
    const char * const pOptionStr
)
{
    if (curOpmode == INIT)
    {
        return newOpmode;
    }
    else
    {
        DU_WARN("'%s' ignored due to conflicting options set\n", pOptionStr);
        return curOpmode;
    }
}

// Clears dupkg saved in context store
static DU_RCODE clearDupkgPathFromCtxStore(PCONTENT_SERVER pServer)
{
    uint64_t retLen = 0U;
    uint8_t  dummy = 0U;
    if (!pServer->bUseContextStore)
    {
        DU_ERR("Context store is not available\n");
        return CONTENT_ERR_GENERIC;
    }
    else
    {
        if (dulinkWrite(pServer->content_provider.persistCtxPath, 0,
                        0, &dummy, &retLen) != DU_OK)
        {
            DU_WARN("Failed to clear ctx_store\n");
            pServer->bUseContextStore = false;
            return CONTENT_ERR_GENERIC;
        }
    }
    return DU_OK;
}

// Load DUPKG path from Context Store
static DU_RCODE loadDupkgPathFromCtxStore(PCONTENT_SERVER pServer)
{
    DU_RCODE duRet = DU_OK;
    uint64_t retLen = 0U;
    if (pServer->bUseContextStore)
    {
        duRet = dulinkRead(pServer->content_provider.persistCtxPath,
            0, sizeof(pServer->dupkgPathCached), pServer->dupkgPathCached, &retLen);
        if (duRet != DU_OK)
        {
            pServer->bUseContextStore = false;
        }
        else
        {
            pServer->dupkgPathCached[retLen] = '\0';
            DU_DBG("DUPKG from ctx store: %s (%lu)\n", pServer->dupkgPathCached, retLen);
        }
    }
    else
    {
        DU_ERR("Failed to load dupkg path: Context store is not available\n");
        duRet = CONTENT_ERR_GENERIC;
    }
    return duRet;
}

// Save DUPKG path to Context Store
static DU_RCODE saveDupkgPathToCtxStore(PCONTENT_SERVER pServer, const char *pPath)
{
    DU_RCODE duRet = DU_OK;
    uint64_t retLen = 0U;
    char tmpBuf[DULINK_MAX_PATH];
    if (pServer->bUseContextStore)
    {
        (void)strlcpy(tmpBuf, pPath, sizeof(tmpBuf));
        duRet = dulinkWrite(pServer->content_provider.persistCtxPath,
            0, strlen(pPath), tmpBuf, &retLen);
        if (duRet != DU_OK)
        {
            pServer->bUseContextStore = false;
        }
        else
        {
            DU_DBG("Saved '%s' to context store\n", pPath);
        }
    }
    else
    {
        DU_ERR("Failed to save dupkg path: Context store is not available\n");
        duRet = CONTENT_ERR_GENERIC;
    }
    return duRet;
}

static void cli_Help(void)
{
    (void)printf("help           Prints this message\n");
    (void)printf("status         Displays various information\n");
    (void)printf("serve [dupkg]  Starts serving DUPKG. "
                           "If <dupkg> is missing, the previously used "
                           "dupkg will be used if any\n");
    (void)printf("stop           Stop serving dupkg\n");
    (void)printf("clear          Clear dupkg path in context store\n");
    (void)printf("set <dupkg>    Set dupkg path locally\n");
    (void)printf("save <dupkg>   Save dupkg path in context store\n");
    (void)printf("quit           Quit\n");
    (void)printf("\n");
}

static void cli_Status(PCONTENT_SERVER pServer)
{
    char     buf[DULINK_MAX_PATH];
    uint64_t retLen = 0U;
    DU_RCODE duRet;
    duMutexLock(&pServer->content_provider.mutex);
    (void)printf("serving               : %s\n",
            pServer->content_provider.state == CONTENT_STATE_CONFIGURED ?  "Yes" : "No");
    if (pServer->bUseContextStore)
    {
        (void)printf("context store         : Used\n");
        duRet = dulinkRead(pServer->content_provider.persistCtxPath,
            0, sizeof(buf), buf, &retLen);
        if (duRet == DU_OK)
        {
            buf[retLen] = '\0';
            (void)printf("DUPKG path (ctx_store): '%s'\n", buf);
        }
        else
        {
            (void)printf("DUPKG path (ctx_store): (failed to read)\n");
        }
    }
    else
    {
        (void)printf("context store         : Not used\n");
    }
    (void)printf("DUPKG path (cached)   : '%s'\n", pServer->dupkgPathCached);
    (void)printf("DUPKG path (in use)   : '%s'\n", pServer->localPath);
    (void)printf("Last Command (Result) : %u (%x)\n",
            pServer->lastCmdId, pServer->lastCmdResult);
    duMutexUnlock(&pServer->content_provider.mutex);
}

static void cli_Clear(PCONTENT_SERVER pServer)
{
    duMutexLock(&pServer->content_provider.mutex);
    (void)memset(pServer->dupkgPathCached, 0, sizeof(pServer->dupkgPathCached));
    (void)clearDupkgPathFromCtxStore(pServer);
    duMutexUnlock(&pServer->content_provider.mutex);
}

static void cli_Set(PCONTENT_SERVER pServer, const char *pPath)
{
    duMutexLock(&pServer->content_provider.mutex);
    (void)strlcpy(pServer->dupkgPathCached, pPath, sizeof(pServer->dupkgPathCached));
    duMutexUnlock(&pServer->content_provider.mutex);
}

static void cli_Save(PCONTENT_SERVER pServer, const char *pPath)
{
    duMutexLock(&pServer->content_provider.mutex);
    (void)saveDupkgPathToCtxStore(pServer, pPath);
    duMutexUnlock(&pServer->content_provider.mutex);
}

static void cli_Serve(PCONTENT_SERVER pServer, const char *pPath)
{

    duMutexLock(&pServer->content_provider.mutex);
    if (strlen(pPath) != 0U)
    {
        DU_INFO("Serve DUPKG '%s' as specified\n", pPath);
        (void)executeCmd(pServer, CMD_SERVE, pPath);
    }
    else if (strlen(pServer->dupkgPathCached) != 0U)
    {
        DU_INFO("Serve DUPKG '%s' locally cached\n", pServer->dupkgPathCached);
        (void)executeCmd(pServer, CMD_SERVE, pServer->dupkgPathCached);
    }
    else
    {
        DU_INFO("DUPKG is not set. Attempting to load from ctx store\n");
        (void)loadDupkgPathFromCtxStore(pServer);
        if (strlen(pServer->dupkgPathCached) == 0U)
        {
            DU_ERR("There is no DUPKG path available from ctx store\n");
        }
        else
        {
            (void)executeCmd(pServer, CMD_SERVE, pServer->dupkgPathCached);
        }
    }
    duMutexUnlock(&pServer->content_provider.mutex);
}

static void cli_Stop(PCONTENT_SERVER pServer)
{
    duMutexLock(&pServer->content_provider.mutex);
    DU_INFO("Stopping %s\n", pServer->localPath);
    (void)executeCmd(pServer, CMD_STOP_SERVE, "");
    duMutexUnlock(&pServer->content_provider.mutex);
}

static bool isBackground(void)
{
    return getpgrp() != tcgetpgrp(0);
}

// Runs CLI for content server
static int run_CLI(PCONTENT_SERVER pServer, CLI_OP_MODE initMode)
{
    struct pollfd fds[2] = {};

    char cmdBuf[DU_MAX_CMD_LEN];
    char cmd[DU_MAX_CMD_LEN];
    char arg[DU_MAX_CMD_LEN];
    int numTokens;

    fds[0].fd = fileno(stdin);
    fds[0].events = POLLIN;
    fds[1].fd = pServer->pipeFd[0];
    fds[1].events = POLLIN;

    if (initMode == SERVE_RESTORE)
    {
        duMutexLock(&pServer->content_provider.mutex);
        if (loadDupkgPathFromCtxStore(pServer) == DU_OK)
        {
            if (strlen(pServer->dupkgPathCached) == 0U)
            {
                DU_WARN("Empty DUPKG path returned from Context Store.\n");
            }
            else
            {
                DU_INFO("Serve DUPKG previously used '%s'\n", pServer->dupkgPathCached);
                (void)executeCmd(pServer, CMD_SERVE, pServer->dupkgPathCached);
            }
        }
        else
        {
            DU_ERR("Failed to load DUPKG previously stored.\n");
        }
        duMutexUnlock(&pServer->content_provider.mutex);
    }
    else
    {
        if (initMode == SERVE_NEW)
        {
            duMutexLock(&pServer->content_provider.mutex);
            DU_INFO("Serve DUPKG at '%s'\n", pServer->dupkgPathCached);
            (void)executeCmd(pServer, CMD_SERVE, pServer->dupkgPathCached);
            duMutexUnlock(&pServer->content_provider.mutex);
        }
    }

    // Enter CLI
    for (;;)
    {
        if (isBackground())
        { /* if it's background, then do nothing in CLI loop */
            (void)sleep(100);
            continue;
        }
        else
        {
            duMutexLock(&pServer->content_provider.mutex);
            if (pServer->content_provider.state == CONTENT_STATE_CONFIGURED)
            {
                printf("CONTENT-SERVER[%s]> ", pServer->localPath);
            }
            else
            {
                printf("content-server> ");
            }
            duMutexUnlock(&pServer->content_provider.mutex);
            (void)fflush(stdout);
        }
        if (poll(fds, 2, -1) == -1)
        {
            DU_ERR("poll() error\n");
            break;
        }

        if (((uint32_t)fds[1].revents & (uint32_t)POLLIN) != 0U)
        {
            (void)close(pServer->pipeFd[0]);
            break;
        }

        if (((uint32_t)fds[0].revents & (uint32_t)POLLIN) != 0U)
        {
            if (fgets(cmdBuf, (int)sizeof(cmdBuf), stdin) == NULL)
            {
                if (isBackground())
                {
                    continue;
                }
                if (feof(stdin))
                {
                   clearerr(stdin);
                   printf("\n");
                   continue;
                }
                break;
            }

            numTokens = sscanf(cmdBuf, "%s %s",  cmd, arg);
            if (numTokens < 1)
            {
                (void)printf("'help' for usage\n");
                continue;
            }

            if (strcmp(cmd, "help") == 0)
            {
                cli_Help();
            }
            else if (strcmp(cmd, "status") == 0)
            {
                cli_Status(pServer);
            }
            else if (strcmp(cmd, "clear") == 0)
            {
                cli_Clear(pServer);
            }
            else if (strcmp(cmd, "set") == 0)
            {
                cli_Set(pServer,  numTokens == 2 ? arg : "");
            }
            else if (strcmp(cmd, "save") == 0)
            {
                cli_Save(pServer, numTokens == 2 ? arg : "");
            }
            else if (strcmp(cmd, "serve") == 0)
            {
                cli_Serve(pServer, numTokens == 2 ? arg : "");
            }
            else if (strcmp(cmd, "stop") == 0)
            {
                cli_Stop(pServer);
            }
            else if (strcmp(cmd, "quit") == 0)
            {
                break;
            }
            else
            {
                printf("Unknown command: %s\n", cmd);
            }
        }
    }
    printf("\n");
    return 0;
}

static int run(PCONTENT_SERVER pServer, CLI_OP_MODE opMode)
{
    int ret;
    switch (opMode)
    {
        case EXIT_CLEAR_CTX:
            ret = clearDupkgPathFromCtxStore(pServer) == DU_OK ? 0 : 1;
            break;
        case SERVE_NEW:
        case SERVE_RESTORE:
        case SERVE_IDLE:
            ret = run_CLI(pServer, opMode);
            break;
        default:
            ret = 1;
            break;
    }
    cleanUp();
    return ret;
}

/* ------------------------ Main -------------------------------------------- */
int main(int argc, char* argv[])
{
    DU_RCODE duRet;
    pthread_t tid;
    CLI_OP_MODE opMode = INIT;

    // Constructs content server
    contentServerConstruct(&content_server);

    // Parses arguments
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            usage();
            return 0;
        }
        else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--clear") == 0 ||
                 strcmp(argv[i], "-a") == 0)
        {
            opMode = setOpMode(opMode, EXIT_CLEAR_CTX, argv[i]);
        }
        else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--resume") == 0)
        {
            opMode = setOpMode(opMode, SERVE_RESTORE, argv[i]);
        }
        else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--idle") == 0)
        {
            opMode = setOpMode(opMode, SERVE_IDLE, argv[i]);
        }
        else if (argv[i][0] != '-' && opMode == INIT)
        {
            opMode = setOpMode(opMode, SERVE_NEW, argv[i]);
            (void)strlcpy(content_server.dupkgPathCached, argv[i], DULINK_MAX_PATH);
        }
        else
        {
            DU_WARN("'%s': unsupported option\n", argv[i]);
        }
    }

    if (opMode == INIT)
    {
        opMode = SERVE_RESTORE;
    }

    // Block SIGINT/SIGTERM signals as the these signals are handled in a
    // dedicated thread
    (void)sigemptyset(&signal_mask);
    (void)sigaddset(&signal_mask, SIGINT);
    (void)sigaddset(&signal_mask, SIGTERM);
    (void)sigaddset(&signal_mask, SIGTTIN);

    if (pthread_sigmask(SIG_BLOCK, &signal_mask, NULL) != 0) {
        DU_ERR("error sigmask\n");
        return 1;
    }

    // Start a thread that monitors SIGINT/SIGTERM signals and notifies the
    // main CLI thread
    if (pthread_create(&tid, NULL, thread_SignalMon, &content_server) != 0) {
        DU_ERR("Error starting the clean-up thread\n");
        return 1;
    }

    duRet = contentServerInit(&content_server);
    if (duRet != DU_OK)
    {
        DU_ERR("Installer initialization failed\n");
        return 1;
    }
    return run(&content_server, opMode);
}
