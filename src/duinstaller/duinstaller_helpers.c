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
#include "ducmd_parser.h"
#include "duinstaller.h"
#include "duinstaller_helpers.h"
#include "duplugin.h"
#include "plugin_common/duinstaller_common.h"

/* ------------------------ System Includes --------------------------------- */
#include <stdbool.h>
#include <unistd.h>
#include <libgen.h>

/* ------------------------ Defines ----------------------------------------- */
// Max tokens to use when parsing command input
#define MAX_TOKENS (64)
// Max length to read in one chunk via DU Link
#define MAX_CHUNK_LEN (1024 * 16)

#define STAGED_SUFFIX ".staged"

// Ensure max chunk size <= max supported by DU Link
DU_CT_ASSERT(MAX_CHUNK_LEN <= DULINK_MAX_DATA_SIZE);

/* ------------------------ Public Functions -------------------------------- */
DU_RCODE parseCmd
(
    const char *pCmdInput,
    uint8_t    *pCmdId,
    char       *pFilePath,
    uint32_t    filePathLen,
    char       *pSaveName,
    uint32_t    saveNameLen
)
{
    DU_RCODE duRet  = DU_OK;
    char     tokenizeBuf[DUINSTALLER_MAX_CMD_LEN] = {0};
    char     cmdBuf[DUINSTALLER_MAX_CMD_LEN] = {0};
    uint32_t i;
    bool     bFound = false;

    CMD_TOKEN  tokens[MAX_TOKENS] = {{0}};
    uint32_t   numTokens = 0;
    const char * const pTagPath = "path=";
    const char * const pTagSavename = "savename=";

    // Create a copy for tokenization
    if (strlen(pCmdInput) > DUINSTALLER_MAX_CMD_LEN)
    {
        DU_ERR("Cmd input too long\n");
        return SAMPLE_ERR_BUFFER_TOO_SMALL;
    }
    (void) strlcpy(tokenizeBuf, pCmdInput, DUINSTALLER_MAX_CMD_LEN);

    duRet = parseStringIntoTokens(tokenizeBuf, &numTokens, tokens, MAX_TOKENS);
    if (duRet != DU_OK || numTokens == 0)
    {
        DU_ERR("Invalid cmd input\n");
        return SAMPLE_ERR_INVALID_ARGUMENT;
    }

    // Match first token to possible commands in SAMPLE_CMDS_TABLE
    (void) strlcpy(cmdBuf, tokenizeBuf, tokens[0].size + 1U);
    for (i = 0; i < DU_ARRAY_SIZE(SAMPLE_CMDS_TABLE); i++)
    {
        if (strcmp(cmdBuf, SAMPLE_CMDS_TABLE[i]) == 0)
        {
            *pCmdId = i;
            bFound = true;
            break;
        }
    }
    if (!bFound)
    {
        DU_ERR("Invalid cmd %s\n", cmdBuf);
        return SAMPLE_ERR_INVALID_ARGUMENT;
    }

    // Load additional args for deploy cmd
    if (*pCmdId == SAMPLE_CMD_ID_DEPLOY)
    {
        duRet = stripTagFromInput(tokenizeBuf, tokens, numTokens, pTagPath,
                                pFilePath, filePathLen);
        if (duRet != DU_OK)
        {
            DU_ERR("Could not load DU Link path of file to install\n");
            return SAMPLE_ERR_INVALID_ARGUMENT;
        }

        duRet = stripTagFromInput(tokenizeBuf, tokens, numTokens, pTagSavename,
                                  pSaveName, saveNameLen);
        if (duRet == DUCOMMON_ERR_NOT_FOUND)
        {
            duRet = DU_OK;
            *pSaveName = '\0';
        }
        else if (duRet != DU_OK)
        {
            DU_ERR("Error occurred parsing cmd input\n");
            return SAMPLE_ERR_GENERIC;
        }
        else
        {
            // OK
        }
    }

    return duRet;
}

DU_RCODE execDeployCmd
(
    PSAMPLE_INSTALLER pMyInstaller
)
{
    DU_RCODE            duRet = DU_OK;
    uint64_t            lenToRead;
    uint64_t            bytesRemaining;
    static uint8_t      dataBuf[MAX_CHUNK_LEN];
    uint64_t            bytesRead;
    char               *pSaveName;
    FILE               *pInstallFile;
    uint64_t            offset = 0;

    if (dulinkGetSize(pMyInstaller->filePathBuf, &bytesRemaining) != DU_OK)
    {
        DU_ERR("Could not get size of file at %s\n", pMyInstaller->filePathBuf);
        duRet = SAMPLE_ERR_GENERIC;
        goto bailout;
    }

    if (strlen(pMyInstaller->savenameBuf) > 0)
    {
        pSaveName = pMyInstaller->savenameBuf;
    }
    else
    {
        // Strip filname off DU Link file
        (void) strlcpy(pMyInstaller->savenameBuf, pMyInstaller->filePathBuf,
            DULINK_MAX_PATH);
        pSaveName = basename(pMyInstaller->savenameBuf);
    }

    // Generate savepath
    if (duPathJoin(pMyInstaller->installDir, pSaveName,
        pMyInstaller->stagedFilePathBuf, INSTALL_PATH_MAX_LEN) == NULL)
    {
        DU_ERR("Failed to generate save path for file installation\n");
        duRet = SAMPLE_ERR_GENERIC;
        goto bailout;
    }
    // Add .staged suffix
    if (strlen(pMyInstaller->stagedFilePathBuf) + strlen(STAGED_SUFFIX) >
        INSTALL_PATH_MAX_LEN)
    {
        DU_ERR("Not enough space to generate staging filepath\n");
        duRet = SAMPLE_ERR_BUFFER_TOO_SMALL;
        goto bailout;
    }
    (void) strlcat(pMyInstaller->stagedFilePathBuf, STAGED_SUFFIX, sizeof(pMyInstaller->stagedFilePathBuf));

    // Open file for writing
    pInstallFile = fopen(pMyInstaller->stagedFilePathBuf, "wb");
    if (pInstallFile == NULL)
    {
        DU_ERR("Could not open file %s for writing\n",
            pMyInstaller->stagedFilePathBuf);
        duRet = SAMPLE_ERR_GENERIC;
        goto bailout;
    }

    while (bytesRemaining > 0)
    {
        // Chunking required if size of file is > DU Link max transfer size
        lenToRead = duMin(bytesRemaining, MAX_CHUNK_LEN);
        duRet = dulinkReadRetry(pMyInstaller->filePathBuf, offset, lenToRead,
            dataBuf, &bytesRead);
        if (duRet != DU_OK)
        {
            DU_ERR("Failed to read from DU Link path %s\n",
                pMyInstaller->filePathBuf);
            duRet = SAMPLE_ERR_GENERIC;
            break;
        }

        if (fwrite(dataBuf, bytesRead, 1, pInstallFile) == 0)
        {
            DU_ERR("Failure occurred during write\n");
            duRet = SAMPLE_ERR_GENERIC;
            break;
        }

        bytesRemaining -= bytesRead;
        offset += bytesRead;
    }

    fclose(pInstallFile);

bailout:
    return duRet;
}

DU_RCODE execCommitCmd
(
    PSAMPLE_INSTALLER pMyInstaller
)
{
    DU_RCODE duRet = DU_OK;
    char     commitSavePath[INSTALL_PATH_MAX_LEN];
    char    *pStagedSuffix;
    int32_t  fd;

    duMutexLock(&pMyInstaller->duInstaller.mutex);
    DU_ASSERT(pMyInstaller->duInstaller.state == INSTALLER_STATE_PENDING_COMMIT);
    duMutexUnlock(&pMyInstaller->duInstaller.mutex);

    // Open the staged file
    fd = open(pMyInstaller->stagedFilePathBuf, O_RDONLY);
    if (fd == -1)
    {
        DU_ERR("Expected staged file %s does not exist!\n",
            pMyInstaller->stagedFilePathBuf);
        duRet = SAMPLE_ERR_NOT_FOUND;
        goto bailout;
    }

    // Get new savename, strip .staged suffix
    (void) strlcpy(commitSavePath, pMyInstaller->stagedFilePathBuf, sizeof(commitSavePath));
    pStagedSuffix = strstr(commitSavePath, STAGED_SUFFIX);
    if (pStagedSuffix == NULL)
    {
        DU_ERR("Unexpected staged filename\n");
        duRet = SAMPLE_ERR_GENERIC;
        goto bailout;
    }
    *pStagedSuffix = '\0';

    // Rename the file using the file descriptor
    if (renameat(AT_FDCWD, pMyInstaller->stagedFilePathBuf, fd, commitSavePath) != 0)
    {
        DU_ERR("Committing file failed\n");
        duRet = SAMPLE_ERR_GENERIC;
    }

bailout:
    if (fd != -1) {
        // Close the file descriptor
        (void) close(fd);
    }

    return duRet;
}
