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
 * @file  duinstaller.h
 * @brief Data structures and definitions for DU Installer
 *
 * This header is meant to act as a starting point for developers creating their
 * own installer components based on the Drive Update installer spec. It
 * implements a DU Installer plugin.
 */

#ifndef DUINSTALLER_H_
#define DUINSTALLER_H_

/* ------------------------ Drive Update Includes --------------------------- */
#include "ducommon.h"
#include "dulink.h"
#include "plugin_common/duinstaller_common.h"

/* ------------------------ System Includes --------------------------------- */
#include <stdbool.h>

/* ------------------------ Installer Defines ------------------------------- */
#define SAMPLE_VERSION              "1"

// Error Codes
#define SAMPLE_OK                   (DU_OK)
#define SAMPLE_ERR_GENERIC          (1)
#define SAMPLE_ERR_INVALID_ARGUMENT (2)
#define SAMPLE_ERR_NOT_FOUND        (3)
#define SAMPLE_ERR_BUFFER_TOO_SMALL (4)
#define SAMPLE_ERR_NOT_SUPPORTED    (5)
#define SAMPLE_ERR_DULINK           (6)

#define SAMPLE_CMD_ID_DEPLOY        (0)
#define SAMPLE_CMD_ID_COMMIT        (1)
#define SAMPLE_CMD_ID_CLEAR_ERROR   (2)
#define NUM_SAMPLE_CMDS             (3)

#define INSTALL_PATH_MAX_LEN        (512)

/* ------------------------ Installer Data Structures ----------------------- */
typedef struct SAMPLE_INSTALLER SAMPLE_INSTALLER, *PSAMPLE_INSTALLER;

struct SAMPLE_INSTALLER
{
    /// Contains data structures required by all installers.
    /// Owns the single mutex/cond used by all callbacks and state functions.
    DU_INSTALLER duInstaller;
    /// Set when an update command fails; cleared on transition to IDLE
    bool bUpdateFailed;
    /// ID of the command currently being processed
    uint8_t cmdId;
    /// DU Link path of file to read for installation
    char filePathBuf[DULINK_MAX_PATH];
    /// File name to use when saving file during installation
    char savenameBuf[DULINK_MAX_PATH];
    /// Full path of .staged file saved during deployment
    char stagedFilePathBuf[INSTALL_PATH_MAX_LEN];
    /// Installation directory path. Installed files will be saved here
    char installDir[INSTALL_PATH_MAX_LEN];
    /// Buffer to store installer.cmp result
    char installerCmpBuf[(MAX_INSTALLER_VER_LENGTH * 2) + 1];
};

/// Global installer object
extern SAMPLE_INSTALLER gInstaller;

/// Table to store possible commands for the installer
extern const char * SAMPLE_CMDS_TABLE[NUM_SAMPLE_CMDS];

/* ------------------------ Public Functions -------------------------------- */
/*!
 * Initialize installer data structures and export DU Link Nodes
 *
 * @param[in] pInstaller
 *      Pointer to uninitialized installer data structure
 *
 * @return DU_OK
 *      Initialized successfully
 * @return SAMPLE_ERR_GENERIC
 *      Failed to initialize installer
 */
DU_RCODE installerInit
(
    PSAMPLE_INSTALLER pInstaller
);

/*!
 * Main loop for execution of installer context
 *
 * @param[in] pInstaller
 *      Pointer to initialized installer data structure
 *
 * @return DU_OK
 *      Scheduler loop completed and exiting normally
 * @return SAMPLE_ERR_GENERIC
 *      Scheduler has encountered and cannot recover
 */
DU_RCODE installerRun
(
    PSAMPLE_INSTALLER pInstaller
);

#endif
