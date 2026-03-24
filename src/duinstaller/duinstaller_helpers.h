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
 * @file  duinstaller_helpers.h
 * @brief Helper functions for DU Installer
 *
 */

#ifndef DUINSTALLER_HELPERS_H_
#define DUINSTALLER_HELPERS_H_

/* ------------------------ Drive Update Includes --------------------------- */
#include "ducommon.h"
#include "dulink.h"
#include "plugin_common/duinstaller_common.h"
#include "duinstaller.h"

/* ------------------------ System Includes --------------------------------- */
#include <stdbool.h>

/* ------------------------ Public Functions -------------------------------- */
/*!
 * Parse command input string into command name and relevant arguments
 *
 * @param[in] pCmdInput
 *      Pointer to raw input command sent to cmd node
 * @param[out] pCmdId
 *      ID of command parsed, from SAMPLE_CMDS_TABLE
 * @param[out] pFilePath
 *      Output buffer used to store path of file to install in local directory
 * @param[in] filePathLen
 *      Max length of file path buffer
 * @param[out] pSaveName
 *      Output buffer used to store alternate save name of file if applicable
 * @param[in] saveNameLen
 *      Length of pContentRoot buffer
 *
 * @return DU_OK
 *      Command successfully parsed
 * @return SAMPLE_ERR_INVALID_ARGUMENT
 *      Invalid command or required fields not found
 * @return SAMPLE_ERR_GENERIC
 *      Other error occurred
 */
DU_RCODE parseCmd
(
    const char *pCmdInput,
    uint8_t    *pCmdId,
    char       *pFilePath,
    uint32_t    filePathLen,
    char       *pSaveName,
    uint32_t    saveNameLen
);

/*!
 * Execute deploy command. When deploy command is executed on installer,
 * it will create a new file under the local installer directory with name
 * either set by savename=<NAME> when the command was sent, or if not set in
 * the command, name of the DU Link file will be used. Upon success, a new file
 * <NAME>.staged will be created, and installer will be able to move to
 * PENDING_COMMIT state. When the commit command is sent later, the file will
 * be saved as <NAME>, overwriting any file which may previously have held the
 * same name
 *
 * Example: "deploy path=/content/MY_DATA savename=data.txt"
 *  - Will read contents from /content/MY_DATA and save the contents to a file
 *    called "data.txt.staged" under the installation directory
 *
 * Assumes internal parameters are already initialized
 *
 * @param[in] pMyInstaller
 *      Pointer to installer data structure
 *
 * @return DU_OK
 *      Deploy command completed successfully
 * @return SAMPLE_ERR_GENERIC
 *      Failure occurred during installation
 */
DU_RCODE execDeployCmd
(
    PSAMPLE_INSTALLER pMyInstaller
);


/*!
 * Execute commit command. The commit command may only be executed when the installer is in
 * installer is in PENDING_COMMIT state. This command will commit any staged
 * file which was deployed via the deploy command. The file created by deploy,
 * <NAME>.staged will be renamed to <NAME>, overwriting any file which in the
 * install directory of the same name
 *
 * Assumes internal parameters are already initialized
 *
 * @param[in] pMyInstaller
 *      Pointer to installer data structure
 *
 * @return DU_OK
 *      Commit command completed successfully
 * @return SAMPLE_ERR_GENERIC
 *      Failure occurred during commit
 */
DU_RCODE execCommitCmd
(
    PSAMPLE_INSTALLER pMyInstaller
);

#endif