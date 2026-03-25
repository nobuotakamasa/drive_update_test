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
 * @file  remote_content_provider.c
 * @brief Implementation of content server
 */

/* ------------------------ Drive Update Includes --------------------------- */
#include "remote_content_provider.h"

/* ------------------------ System Includes --------------------------------- */
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <signal.h>

/* ------------------------ Defines ----------------------------------------- */
typedef enum CONTENT_STATE CONTENT_STATE;
typedef enum CONTENT_CMD CONTENT_CMD;

// Possible states
enum CONTENT_STATE
{
    STATE_INIT = 0U,
    STATE_IDLE,
    STATE_CONFIGURED,
    CONTENT_STATE_MAX
};

char * const STATES_TABLE[CONTENT_STATE_MAX] =
{
    [STATE_INIT] = "INIT",
    [STATE_IDLE] = "IDLE",
    [STATE_CONFIGURED] = "CONFIGURED"
};

// Possible commands
enum CONTENT_CMD
{
    CMD_SERVE = 0U,
    CMD_STOP_SERVE,
    CMD_MAX
};

char * const CMDS_TABLE[CMD_MAX] =
{
    [CMD_SERVE] = "serve",
    [CMD_STOP_SERVE] = "stop_serving"
};

struct MemoryStruct
{
    char *pMemory;
    uint64_t size;
};

// The total number to return DULINK_CB_ERR_BUSY_RETRY, in this example, the max
// value of it is DULINK_RETRY_NUMBER(10U), and we can set its max value based
// our need, and also we can reset it based our need.
static uint32_t totalRetry = 0U;
/* ------------------------ DU Link Node Definitions ------------------------ */
static CTX_LOCK_STR ctxPluginType =
{
    .pStr = PLUGIN_TYPE_CONTENT_PROVIDER,
    .pMutex = NULL
};

static CTX_LOCK_STR ctxState =
{
    .pStr = content_server.stateStr,
    .pMutex = &content_server.mutex
};

// Contexts for runlevels
static CTX_LOCK_RL ctxCurrentRl =
{
    .pRl = &content_server.currentRl,
    .pMutex = &content_server.mutex
};

static CTX_LOCK_RL ctxPendingRl =
{
    .pRl = &content_server.pendingRl,
    .pMutex = &content_server.mutex
};

// Contexts for .list nodes
static CTX_LIST_STR ctxStateList =
{
    .size = CONTENT_STATE_MAX,
    .ppStrList = (char const **) STATES_TABLE
};

static CTX_LIST_STR ctxCmdList =
{
    .size = CMD_MAX,
    .ppStrList = (char const **) CMDS_TABLE
};

// Table containing all file nodes to export
static const DULINK_EXPORT_REQS CONTENT_DULINK_FILES_TABLE[] =
{
    // plugin-type
    { .pPath = NODE_PLUGIN_TYPE, .attr = READ_ONLY_ATTR,
      .cb = readOnlyStringCB, .pCtx = &ctxPluginType },
    // requested_rl
    { .pPath = NODE_REQUESTED_RL, .attr = MASTER_ONLY_RW_ATTRIBUTE,
      .cb = requestedRLCB, .pCtx = &content_server },
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

/* ------------------------ Global ------------------------------------------ */
CONTENT_SERVER content_server = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cmd = {0},
    .state = STATE_INIT,
    .stateStr = "INIT",
    .currentRl = RL_DORMANT,
    .pendingRl = RL_INVALID,
    .requestedRl = RL_DORMANT,
    .localPath = {0}
};

static uint32_t  gRefId = DULINK_CLOSE_ALL;
/* ------------------------ Functions --------------------------------------- */
/*!
 * Get remote file size from given url to check if file exists.
 */
static DU_RCODE getRemoteFileSize
(
    const char *pUrl,
    curl_off_t *pSize
)
{
    DU_RCODE duRet   = DU_OK;
    int64_t  retCode = 0;
    CURLcode retCurl = CURLE_OK;
    if (pUrl == NULL || pSize == NULL)
    {
        DU_ERR("Invalid argument input for getRemoteFileSize\n");
        return CONTENT_ERR_INVALID_ARGUMENT;
    }

    *pSize = 0U;
    CURL *curlHandle = curl_easy_init();
    if (curlHandle == NULL)
    {
        DU_ERR("curlHandle is NULL\n");
        return CONTENT_ERR_INVALID_ARGUMENT;
    }
    curl_easy_setopt(curlHandle, CURLOPT_URL, pUrl);
    curl_easy_setopt(curlHandle, CURLOPT_CUSTOMREQUEST, "HEAD");
    curl_easy_setopt(curlHandle, CURLOPT_NOBODY, 1);
    retCurl = curl_easy_perform(curlHandle);
    if (retCurl == CURLE_OK)
    {
        curl_easy_getinfo(curlHandle, CURLINFO_RESPONSE_CODE, &retCode);
        if (retCode == HTTP_OK || retCode == HTTP_PARTIAL_CONTENT)
        {
            curl_easy_getinfo(curlHandle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, pSize);
        }
        else
        {
            DU_ERR("return code %ld from server is invalid\n", retCode);
            duRet = CONTENT_ERR_CURL;
        }
    }
    else
    {
        DU_ERR("Failed to set curl perform, curl error code %d\n", retCurl);
        DU_ERR("Error on getRemoteFileSize %s\n", pUrl);
        duRet = CONTENT_ERR_CURL;
    }
    curl_easy_reset(curlHandle);
    curl_easy_cleanup(curlHandle);

    return duRet;
}


/*!
 * Construct range parameter for libcurl range opt.
 *
 */
static DU_RCODE constructRange
(
    uint64_t startOffset,
    uint64_t blockSize,
    uint64_t fileSize,
    char    *pRange
)
{
    char     start[REMOTE_SIZE_MAX] = {0};
    char     end[REMOTE_SIZE_MAX]   = {0};
    uint32_t numSize                = 0;
    uint64_t result                 = 0UL;
    if (pRange == NULL)
    {
        DU_ERR("Invalid argument input for constructRange\n");
        return CONTENT_ERR_INVALID_ARGUMENT;
    }

    (void) ui64ToStr(startOffset, start, REMOTE_SIZE_MAX, 10, &numSize);
    (void) strncat(start, "-", REMOTE_SIZE_MAX - 1);
    if (!AddU64(startOffset, blockSize, &result))
    {
        DU_ERR("Failed to calculate the offset in buffer,"
               "startOffset: %lu, blockSize: %lu\n", startOffset, blockSize);
        return DUCOMMON_ERR_GENERIC;
    }
    if (result < fileSize)
    {
        (void) ui64ToStr(startOffset + blockSize - 1, end, REMOTE_SIZE_MAX, 10, &numSize);
        if ((uint64_t)REMOTE_SIZE_MAX - 1U < (uint64_t)strlen(start))
        {
            DU_ERR("Failed to strncat, strlen(start): %lu\n", strlen(start));
            return DUCOMMON_ERR_GENERIC;
        }
        (void) strncat(start, end, REMOTE_SIZE_MAX - strlen(start) - 1);
        start[REMOTE_SIZE_MAX - 1] = '\0';
    }

    (void) strlcpy(pRange, start, REMOTE_SIZE_MAX);
    return DU_OK;
}

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
    const char * const pTagLocal = "remote_url=";

    // Create a copy for tokenization
    if (strlen(pCmdInput) > DU_MAX_CMD_LEN)
    {
        DU_ERR("Cmd input too long\n");
        return CONTENT_ERR_BUFFER_TOO_SMALL;
    }
    (void) strlcpy(tokenizeBuf, pCmdInput, DU_MAX_CMD_LEN);

    duRet = parseStringIntoTokens(tokenizeBuf, &numTokens, tokens,
                                  MAX_CMD_TOKENS);
    if (duRet != DU_OK || numTokens == 0)
    {
        DU_ERR("Invalid cmd input\n");
        return CONTENT_ERR_INVALID_ARGUMENT;
    }

    // Match first token to possible commands
    (void) strlcpy(cmdBuf, tokenizeBuf, tokens[0].size + 1U);
    for (i = 0; i < DU_ARRAY_SIZE(CMDS_TABLE); i++)
    {
        if (strcmp(cmdBuf, CMDS_TABLE[i]) == 0)
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

void contentServerSetState
(
    PCONTENT_SERVER pServer,
    uint8_t         state
)
{
    if (pServer->state != state && strlen(STATES_TABLE[state]) < 32U)
    {
        pServer->state = state;
        (void) strlcpy(pServer->stateStr, STATES_TABLE[state], sizeof(pServer->stateStr));
        (void) dulinkTriggerNotify(NOTIFY_NODE_STATE, pServer->stateStr,
        strlen(pServer->stateStr));
    }
}

DU_RCODE contentServerInit
(
    PCONTENT_SERVER pServer
)
{
    DU_RCODE duRet;
    DULINK_CONNECT_INFO duConnInfo = {0};

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

    duMutexLock(&pServer->mutex);
    contentServerSetState(pServer, STATE_IDLE);
    duMutexUnlock(&pServer->mutex);

bailout:
    return duRet;
}

DU_RCODE contentServerRun
(
    PCONTENT_SERVER pServer
)
{
    for (;;)
    {
        sleep(5);
    }
    return DU_OK;
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
 * @param[out] **ppJsonRootElmt
 *      Pointer of pointer to root json element
 *
 * @param[in] *pElementArray
 *      Point of json element array
 *
 * @param[in] elementArraySize
 *      Size of  json element array
 *
 * @return DU_OK
 *      Successfully load and tokenize file list
 *
 * @return CONTENT_ERR_GENERIC
 *      Error happened
 */
static DU_RCODE loadFileListAndTokenize
(
    char           *pRootPath,
    char           *pFileBuf,
    uint32_t        fileBufSize,
    jsonElement_t **ppJsonRootElmt,
    char           *pElementArray,
    uint32_t        elementArraySize
)
{
    DU_RCODE            duRet = DU_OK;
    char                remotePath[DU_PATH_MAX];
    curl_off_t          numBytes;
    CURL               *curlHandle;
    struct MemoryStruct chunk;
    json_t              *pJson = NULL;

    chunk.pMemory = malloc(1);
    if (chunk.pMemory == NULL)
    {
        DU_ERR("Chunk.pMemory is NULL\n");
        return CONTENT_ERR_BUFFER_TOO_SMALL;
    }
    chunk.size   = 0;

    if (duPathJoin(pRootPath, FILE_LIST_NAME, remotePath,
                   sizeof(remotePath)) == NULL)
    {
        DU_ERR("Failed to construct list_of_files path\n");
        return CONTENT_ERR_BUFFER_TOO_SMALL;
    }

    duRet = getRemoteFileSize(remotePath, &numBytes);
    if (duRet != DU_OK)
    {
        DU_ERR("Fail to get remote list_of_files size: %#x\n", duRet);
        return duRet;
    }

    if (numBytes >= fileBufSize)
    {
        DU_ERR("File list is too large to fit in buffer\n");
        return CONTENT_ERR_BUFFER_TOO_SMALL;
    }

    // Load list_of_files into buf
    curlHandle = curl_easy_init();
    if (curlHandle == NULL)
    {
        DU_ERR("curlHandle is NULL\n");
        return CONTENT_ERR_INVALID_ARGUMENT;
    }
    curl_easy_setopt(curlHandle, CURLOPT_URL, remotePath);
    curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, writeMemoryCB);
    curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, (void *)&chunk);
    if (curl_easy_perform(curlHandle) == CURLE_OK)
    {
        memcpy(pFileBuf, chunk.pMemory, chunk.size);
        // Load and parse json
        pJson = jsonParser(pFileBuf, fileBufSize,
                            pElementArray, elementArraySize);
        if (pJson == NULL)
        {
            DU_ERR("Could not parse JSON\n");
            duRet = CONTENT_ERR_INVALID_ARGUMENT;
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
            duRet = CONTENT_ERR_INVALID_ARGUMENT;
        }
    }
    else
    {
        DU_ERR("Fail to load remote list_of_files\n");
        duRet = CONTENT_ERR_CURL;
    }

    curl_easy_reset(curlHandle);
    curl_easy_cleanup(curlHandle);
    free(chunk.pMemory);
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
 *      Pointer to  json element
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
    char            *pRootPath,
    jsonElement_t   *pJsonElmt,
    bool             bOperation
)
{
    DU_RCODE     duRet = DU_OK;
    curl_off_t   fileSize;
    char         localPath[DU_PATH_MAX];
    char         localName[DU_PATH_MAX];
    char         localType[DU_STR_SHORT_BUF_SIZE];
    char        *pFilename;
    char         nodePath[DULINK_MAX_PATH];
    DULINK_ATTR  dirAttr = READ_ONLY_DIR_ATTR;
    DULINK_ATTR  fileAttr = READ_ONLY_ATTR;
    uint64_t     jsonLen = 0U;

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
    (void) strlcpy(nodePath, CONTENT_FILE_PATH, sizeof(nodePath));
    // Skip ./ prefix
    if ((localName[0] == '.') && (localName[1] == '/'))
    {
        pFilename = &localName[2];
    }
    else
    {
        pFilename = localName;
    }
    if (*pFilename != '\0' && strlen(pFilename) < DULINK_MAX_PATH)
    {
        (void) strlcat(nodePath, "/", sizeof(nodePath));
        (void) strlcat(nodePath, pFilename, sizeof(nodePath));
    }
    // Check if file can be accessed first when register file
    if (bOperation)
    {
        (void) strlcpy(localPath, pRootPath, sizeof(localPath));
        (void) duPathJoin(pRootPath, pFilename, localPath, sizeof(localPath));
        if (strcmp(localType, "dir") != 0 &&
            getRemoteFileSize(localPath, &fileSize) != DU_OK)
        {
            DU_ERR("Failed to access remote file %s\n", localPath);
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
    char            *pRootPath
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
    char *pRootPath
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
    for (jsonIndex = 0U; jsonIndex <= jsonArraySize-1U; jsonIndex++)
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
 *      Successfully unlinked every file from DU LINK
 * @return CONTENT_ERR_GENERIC
 *      Error happened
 */
static DU_RCODE executeCmd
(
    PCONTENT_SERVER  pServer,
    uint8_t          cmdId,
    char            *pLocalPath
)
{
    switch (cmdId)
    {
        case CMD_SERVE:
            if (pServer->state == STATE_CONFIGURED)
            {
                DU_ERR("Already serving some files!\n");
                return CONTENT_ERR_INVALID_ARGUMENT;
            }
            if (serveFolder(pServer, pLocalPath) != DU_OK)
            {
                DU_ERR("Failed to serve file!\n");
                if (stopServe(pLocalPath) != DU_OK)
                {
                    DU_WARN("Error clean already served file\n");
                }
                return CONTENT_ERR_INVALID_ARGUMENT;
            }
            contentServerSetState(pServer, STATE_CONFIGURED);
            (void) strlcpy(pServer->localPath, pLocalPath, DULINK_MAX_PATH);
            break;
        case CMD_STOP_SERVE:
            if (pServer->state != STATE_CONFIGURED)
            {
                DU_ERR("Not currently serving files!\n");
                return CONTENT_ERR_INVALID_ARGUMENT;
            }
            if (stopServe(pServer->localPath) != DU_OK)
            {
                DU_ERR("Failed to stop serve file cleanly!\n");
                return CONTENT_ERR_INVALID_ARGUMENT;
            }
            contentServerSetState(pServer, STATE_IDLE);
            (void) memset(pServer->localPath, 0, sizeof(pServer->localPath));
            break;
        default:
            return CONTENT_ERR_INVALID_ARGUMENT;
    }
    return DU_OK;
}
/* ------------------------ Callbacks --------------------------------------- */
uint64_t writeMemoryCB
(
    void    *contents,
    uint64_t size,
    uint64_t nmemb,
    void    *userp
)
{
    uint64_t result = 0UL;
    uint64_t result1 = 0UL;
    uint64_t realsize = 0UL;
    if (!MultU64(nmemb, size, &realsize))
    {
        DU_ERR("Failed to calculate realsize, nmemb:%lu, size:%lu\n", nmemb, size);
        return 0;
    }

    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    if (!AddU64(mem->size, realsize, &result))
    {
        DU_ERR("Failed to calculate ptr,"
               "mem->size: %lu, realsize: %lu\n", mem->size, realsize);
        return 0;
    }
    if (!AddU64(result, 1U, &result1))
    {
        DU_ERR("Failed to calculate ptr,"
               "mem->size + realsize: %lu\n", result);
        return 0;
    }
    char *ptr = realloc(mem->pMemory, result1);
    if (ptr == NULL)
    {
        DU_ERR("Not enough memory\n");
        return 0;
    }

    mem->pMemory = ptr;
    memcpy(&(mem->pMemory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->pMemory[mem->size] = 0;

    return realsize;
}

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
)
{
    DU_RCODE          duRet = DU_OK;
    PCONTENT_SERVER   pServer = (PCONTENT_SERVER) pCtx;
    DU_RUN_LEVEL      rl;
    DU_RUN_LEVEL      oldRl;
    uint8_t           len;
    char              tmpBuf[DU_RUNLEVEL_STR_MAX_SIZE] = {0};

    *pRetVal = 0;

    if (offset != 0)
    {
        DU_ERR("Invalid offset to requested_rl\n");
        duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
        goto bailout;
    }

    duMutexLock(&pServer->mutex);
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
            (void) strlcpy(tmpBuf, pBuf, length + 1U);
            rl = runlevelStrToUint(tmpBuf);
            if (rl == RL_INVALID)
            {
                DU_ERR("Invalid requested_rl %s\n", tmpBuf);
                duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
                break;
            }
            oldRl = pServer->currentRl;
            DU_ASSERT(oldRl < RL_INVALID);
            pServer->currentRl = rl;
            if (pServer->currentRl != oldRl)
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
    duMutexUnlock(&pServer->mutex);

bailout:
    return duRet;
}

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

    duMutexLock(&pServer->mutex);
    switch (operation)
    {
        case DULINK_CB_READ:
        {
            if (length < strlen(pServer->cmd))
            {
                DU_ERR("Not enough space to read cmd\n");
                duRet = DULINK_CB_ERR_UNKNOWN;
                break;
            }
            if (length > 0UL) {
                ((char *)pBuf)[length - 1UL] = '\0';
            }
            (void) strlcpy((char *) pBuf, pServer->cmd, length);
            *pRetVal = strlen((char *) pBuf);
            break;
        }
        case DULINK_CB_SIZE:
        {
            *pRetVal = strlen(pServer->cmd);
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
            (void) strlcpy(tmpCmd, (char *) pBuf, length + 1U);

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
            (void) strlcpy(pServer->cmd, tmpCmd, sizeof(pServer->cmd));
            break;
        }
        default:
        {
            duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
            break;
        }
    }
    duMutexUnlock(&pServer->mutex);

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
    DU_RCODE            ret = DU_OK;
    DULINK_CB_RCODE     retCb = DULINK_CB_OK;
    PCONTENT_SERVER     pServer = (PCONTENT_SERVER) pCtx;
    int64_t             retCode = 0;
    int64_t             performRet;
    char                tmpPath[DULINK_MAX_PATH];
    char                contentPath[DULINK_MAX_PATH];
    char                range[REMOTE_SIZE_MAX];
    CURL               *curlHandle;
    curl_off_t          fileSize;
    struct MemoryStruct chunk;

    chunk.pMemory = malloc(1);
    if (chunk.pMemory == NULL)
    {
        DU_ERR("Chunk.pMemory is NULL\n");
        retCb = DULINK_CB_ERR_INVALID_ARGUMENT;
        goto ret;
    }
    chunk.size   = 0;
    *pRetVal = 0;

    if (pRequestPath == NULL)
    {
        DU_ERR("Request path is NULL\n");
        retCb = DULINK_CB_ERR_INVALID_ARGUMENT;
        goto ret;
    }

    duMutexLock(&pServer->mutex);
    if (pServer->state != STATE_CONFIGURED)
    {
        DU_ERR("Not currently serving the file!\n");
        retCb = DULINK_CB_ERR_INVALID_ARGUMENT;
        duMutexUnlock(&pServer->mutex);
        goto ret;
    }
    // Skip common prefix
    if ((uint64_t)strlen(pRequestPath + strlen(CONTENT_FILE_FULL_PATH)) > sizeof(tmpPath))
    {
        DU_ERR("Failded to strcpy\n");
        retCb = DULINK_CB_ERR_INVALID_ARGUMENT;
        goto ret;
    }
    (void) strlcpy(tmpPath, pRequestPath+strlen(CONTENT_FILE_FULL_PATH), sizeof(tmpPath));
    (void) duPathJoin(pServer->localPath, tmpPath,
                      contentPath, sizeof(contentPath));
    duMutexUnlock(&pServer->mutex);

    switch (operation)
    {
        case DULINK_CB_READ:
            ret = getRemoteFileSize(contentPath, &fileSize);
            if (ret != DU_OK)
            {
                DU_ERR("Failed to get remote file size\n");
                if (ret == CONTENT_ERR_CURL && totalRetry < DULINK_RETRY_NUMBER)
                {
                    totalRetry++;
                    retCb = DULINK_CB_ERR_BUSY_RETRY;
                }
                else
                {
                    retCb = DULINK_CB_ERR_IO;
                }
                goto ret;
            }

            if (offset >= (uint64_t) fileSize)
            {
                DU_INFO("Reading at EOF\n");
                *pRetVal = 0;
                goto ret;
            }

            if (constructRange(offset, length, fileSize, range) != DU_OK)
            {
                DU_ERR("Error on construct range parameter\n");
                retCb = DULINK_CB_ERR_IO;
                goto ret;
            }

            curlHandle = curl_easy_init();
            if (curlHandle == NULL)
            {
                DU_ERR("CurlHandle is NULL\n");
                retCb = DULINK_CB_ERR_IO;
                goto ret;
            }
            curl_easy_setopt(curlHandle, CURLOPT_URL, contentPath);
            curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, writeMemoryCB);
            curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, (void *)&chunk);
            curl_easy_setopt(curlHandle, CURLOPT_RANGE, range);

            performRet = curl_easy_perform(curlHandle);
            if (performRet == CURLE_OK)
            {
                curl_easy_getinfo(curlHandle, CURLINFO_RESPONSE_CODE, &retCode);
                if (retCode == HTTP_OK || retCode == HTTP_PARTIAL_CONTENT)
                {
                    (void) memcpy(pBuf, chunk.pMemory, chunk.size);
                    *pRetVal = chunk.size;
                }
                else if (totalRetry < DULINK_RETRY_NUMBER)
                {
                    DU_ERR("return code %ld from server is invalid\n", retCode);
                    totalRetry++;
                    retCb = DULINK_CB_ERR_BUSY_RETRY;
                }
                else
                {
                    DU_ERR("Error on perform connection\n");
                    retCb = DULINK_CB_ERR_IO;
                }
            }
            else if (totalRetry < DULINK_RETRY_NUMBER)
            {
                DU_ERR("Error %ld in curl_easy_perform\n", performRet);
                totalRetry++;
                retCb = DULINK_CB_ERR_BUSY_RETRY;
            }
            else
            {
                DU_ERR("Error %ld in curl_easy_perform\n", performRet);
                retCb = DULINK_CB_ERR_IO;
            }

            curl_easy_reset(curlHandle);
            curl_easy_cleanup(curlHandle);
            free(chunk.pMemory);
            break;

        case DULINK_CB_SIZE:
            ret = getRemoteFileSize(contentPath, &fileSize);
            if (ret != DU_OK)
            {
                DU_ERR("Failed to get remote file size\n");
                if (ret == CONTENT_ERR_CURL && totalRetry < DULINK_RETRY_NUMBER)
                {
                    totalRetry++;
                    retCb = DULINK_CB_ERR_BUSY_RETRY;
                }
                else
                {
                    retCb = DULINK_CB_ERR_IO;
                }
                goto ret;
            }
            if (fileSize < 0)
            {
                DU_ERR("fileSize is too small\n");
                retCb = DULINK_CB_ERR_IO;
                goto ret;
            }
            else
            {
                *pRetVal = (uint64_t)fileSize;
            }
            break;
        default:
            retCb = DULINK_CB_ERR_INVALID_ARGUMENT;
            break;
    }

ret:
    return retCb;
}

static void usage(void)
{
    DU_INFO("Directly use url to dupkg as argument to host files\n");
    DU_INFO("Use remote_content_provider -a to clear context\n");
}

static void deregisterRemoteContent(void)
{
    char        cmdBuf[DU_STR_SHORT_BUF_SIZE] = {0};
    const char *pPath = DUMASTER_PATH "/plugins";
    uint64_t    retLen;
    uint64_t    result = 0UL;

    (void) strlcpy(cmdBuf, "deregister", DU_STR_SHORT_BUF_SIZE);
    if (!AddU64((uint64_t)strlen(cmdBuf), 1U, &result))
    {
        DU_ERR("Failed to dulinkWrite, strlen(cmdBuf): %lu\n", strlen(cmdBuf));
    }
    else
    {
        if (dulinkWrite(pPath, 0, result, cmdBuf, &retLen) != DU_OK)
        {
            DU_ERR("Can't deregister from master\n");
        }
    }
}

static void cleanUp(void)
{
    deregisterRemoteContent();

    if (dulinkClose(gRefId) != DU_OK)
    {
        DU_ERR("Error disconnect DU LINK\n");
    }
}

static void sigHandler(int signum) {
    DU_LOG("remote_cp signal %d received \n", signum);

    if (signum == SIGINT || signum == SIGTERM)
    {
        DU_LOG("Exiting....\n");
        cleanUp();
        exit(EXIT_SUCCESS);
    }
}

int main(int argc, char* argv[])
{
    DU_RCODE duRet;

    if (argc > 2)
    {
        usage();
        return 0;
    }
    if ((argc == 2) && (strcmp(argv[1], "-h") == 0 ||
                        strcmp(argv[1], "--help") == 0))
    {
        usage();
        return 0;
    }

    // Set up signal handler
    struct sigaction sigact =
    {
        .sa_handler = sigHandler,
        .sa_flags = 0
    };
    (void) sigemptyset(&sigact.sa_mask);
    (void) sigaction(SIGINT, &sigact, NULL);
    (void) sigaction(SIGTERM, &sigact, NULL);

    duRet = contentServerInit(&content_server);
    if (duRet != DU_OK)
    {
        DU_ERR("Installer initialization failed\n");
        goto bailout;
    }

    if (argc == 2)
    {
        if (strcmp(argv[1], "-a") != 0)
        {
            DU_INFO("Using url from argument %s\n", argv[1]);
            duRet = executeCmd(&content_server, (uint8_t) CMD_SERVE, argv[1]);
            if (duRet != DU_OK)
            {
                DU_ERR("Failed to export files using argument %d\n", duRet);
            }
        }
    }

    duRet = contentServerRun(&content_server);

bailout:
    return duRet;
}
