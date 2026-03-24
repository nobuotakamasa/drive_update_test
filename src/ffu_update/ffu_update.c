/*
 * SPDX-FileCopyrightText: Copyright (c) 2024, NVIDIA CORPORATION.  All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/*!
 * @file   ffu_update.c
 * @brief  Application to update firmware using FFU file
 */

#include <stdio.h>
#include <stdbool.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>

/* ---------------------------- NV Includes ---------------------------------*/
#include "nvmnand.h"

/* ----------------------- Defines ------------------------------------------*/
#define PATH_MAX (4096U)
#define FW_VERSION_MAX_SIZE (32U)

static struct option long_options[] =
{
    {"help",              no_argument,       NULL,        'h'},
    {"install",           no_argument,       NULL,        'i'},
    {"status",            no_argument,       NULL,        's'},
    {"version",           no_argument,       NULL,        'v'},
    {"device",            required_argument, NULL,        'd'},
    {"fw",                required_argument, NULL,        'f'},
    {NULL,                0,                 NULL,          0}
};

typedef enum {
    COMMAND_INSTALL = 0U,
    COMMAND_STATUS,
    COMMAND_VERSION,
    COMMAND_MAX
} COMMAND_TYPE;


static void usage(void)
{
    printf( "NVIDIA DRIVE EMMC/UFS Firmware Update Utility For Linux\n"
            "\n\nUsage:"
            "\n\t--install, -i"
            "\n\t\tInstall firmware to the device"
            "\n\t\tffu_update -i -d <device node> -f <path of FW>"
            "\n\t\tffu_update --install -d <device node> -f <path of FW>"
            "\n\t--status, -s"
            "\n\t\tCheck the status of the device"
            "\n\t\tffu_update -s -d <device node>"
            "\n\t\tffu_update --status -d <device node>"
            "\n\t--version, -v"
            "\n\t\tGet the version of the device FW"
            "\n\t\tffu_update -v -d <device node>"
            "\n\t\tffu_update --version -d <device node>"
            "\n\t--help, -h"
            "\n\t\tPrint this help info"
            "\n\t\tffu_update -h"
            "\n\t\tffu_update --help"
            "\n"
        );
}

static int32_t installFw(char *pDevPath, char *pFwPath)
{
    int32_t ret = 0;
    bool    isFfuReady = false;
    mnand_chip chip;
    MNAND_STATUS mnandRet;
    MNAND_FW_COMPARE fwCompare;

    mnandRet = mnand_open(pDevPath, &chip);
    if (mnandRet != MNAND_OK)
    {
        printf("Error %d in mnand_open %s\n", mnandRet, pDevPath);
        return -1;
    }

    printf("Checking if %s FFU is ready\n", pDevPath);
    mnandRet = mnand_is_ffu_ready(&chip, &isFfuReady);
    if (mnandRet != MNAND_OK)
    {
        printf("Error %d in mnand_is_ffu_ready %s\n", mnandRet, pDevPath);
        ret = -1;
        goto end;
    }
    if (isFfuReady == false)
    {
        printf("%s FFU is not ready\n", pDevPath);
        ret = -1;
        goto end;
    }

    printf("Comparing FW version of %s with %s\n", pDevPath, pFwPath);
    mnandRet = mnand_compare_fw_version(&chip, pFwPath, &fwCompare);
    if (mnandRet != MNAND_OK)
    {
        printf("Error %d in mnand_compare_fw_version, device: %s, file: %s\n",
                mnandRet, pDevPath, pFwPath);
        ret = -1;
        goto end;
    }
    printf("FW version compare result: %d\n", fwCompare);
    if (fwCompare != MNAND_PASSED_FW_VERSION_GREATER)
    {
        printf("New FW version is not larger than current FW version\n");
        ret = -1;
        goto end;
    }

    printf("Updating %s to %s\n", pFwPath, pDevPath);
    mnandRet = mnand_ffu(&chip, pFwPath);
    if (mnandRet != MNAND_OK)
    {
        printf("Error %d in mnand_ffu %s, %s\n", mnandRet, pDevPath, pFwPath);
        ret = -1;
        goto end;
    }

end:
    mnandRet = mnand_close(&chip);
    if (mnandRet != MNAND_OK)
    {
        printf("WARN %d in mnand_close %s\n", mnandRet, pDevPath);
    }

    return ret;
}

static int32_t GetStatus(char *pDevPath, MNAND_FFU_STATUS *pFfuStatus)
{
    int32_t ret = 0;
    MNAND_STATUS mnandRet;
    mnand_chip chip;

    mnandRet = mnand_open(pDevPath, &chip);
    if (mnandRet != MNAND_OK)
    {
        printf("Error %d in mnand_open %s\n", mnandRet, pDevPath);
        return -1;
    }

    mnandRet = mnand_get_ffu_status(&chip, pFfuStatus);
    if (mnandRet != MNAND_OK)
    {
        printf("Error %d in mnand_get_ffu_status %s\n", mnandRet, pDevPath);
        ret = -1;
        goto end;
    }

end:
    mnandRet = mnand_close(&chip);
    if (mnandRet != MNAND_OK)
    {
        printf("WARN %d in mnand_close %s\n", mnandRet, pDevPath);
    }

    return ret;
}

static int32_t GetVersion(char *pDevPath, char *pFwVersion)
{
    int32_t ret = 0;
    MNAND_STATUS mnandRet;
    mnand_chip chip;

    mnandRet = mnand_open(pDevPath, &chip);
    if (mnandRet != MNAND_OK)
    {
        printf("Error %d in mnand_open %s\n", mnandRet, pDevPath);
        return -1;
    }

    mnandRet = mnand_get_fw_version(&chip, pFwVersion);
    if (mnandRet != MNAND_OK)
    {
        printf("Error %d in mnand_get_fw_version %s\n", mnandRet, pDevPath);
        ret = -1;
        goto end;
    }

end:
    mnandRet = mnand_close(&chip);
    if (mnandRet != MNAND_OK)
    {
        printf("WARN %d in mnand_close %s\n", mnandRet, pDevPath);
    }

    return ret;
}

int32_t main(int32_t argc, char* argv[])
{
    int32_t ret = 0;
    int32_t option_index = 0;
    int32_t c = 0;
    char fwPath[PATH_MAX] = {0};
    char devPath[PATH_MAX] = {0};
    char fwVersion[FW_VERSION_MAX_SIZE] = {0};
    MNAND_FFU_STATUS ffuStatus = MNAND_FFU_STATUS_SUCCESS;
    COMMAND_TYPE command = COMMAND_MAX;

    // Process command line options
    while ((c = getopt_long(argc, argv, "hisvd:f:",
                            long_options, &option_index)) != -1)
    {
        switch (c)
        {
            case 'i':
                command = COMMAND_INSTALL;
                break;

            case 's':
                command = COMMAND_STATUS;
                break;

            case 'v':
                command = COMMAND_VERSION;
                break;

            case 'd':
                if (command >= COMMAND_MAX)
                {
                    printf("[-d <device node>] is valid for command %c\n", c);
                    return -1;
                }

                if (strlen(optarg) >= sizeof(devPath))
                {
                    printf("Input device node's size is larger than %lu\n", sizeof(devPath));
                    return -1;
                }
                (void) strcpy(devPath, optarg);
                break;

            case 'f':
                if (command != COMMAND_INSTALL)
                {
                    printf("[-f <path of FW>] is valid for install command only\n");
                    return -1;
                }

                if (strlen(optarg) >= sizeof(fwPath))
                {
                    printf("Input device node's size is larger than %lu\n", sizeof(devPath));
                    return -1;
                }
                (void) strcpy(fwPath, optarg);
                break;

            case 'h':
                usage();
                return 0;

            default:
                printf("Invalid input!\n");
                usage();
                return -1;
        }
    }

    if (command == COMMAND_MAX)
    {
        printf("Invalid input!\n");
        usage();
        return -1;
    }

    // process command
    switch (command)
    {
        case COMMAND_INSTALL:
            printf("Installing FW:%s to device:%s\n", fwPath, devPath);
            ret = installFw(devPath, fwPath);
            printf("Install FW:%s to device:%s: %s\n", fwPath, devPath,
                    ret == 0 ? "Success" : "Failed");
            break;

        case COMMAND_STATUS:
            printf("Getting status of %s FW\n", devPath);
            ret = GetStatus(devPath, &ffuStatus);
            printf("Get status of %s FW: %s\n", devPath, ret == 0 ? "Success" : "Failed");
            if (ret == 0)
            {
                printf("Status of %s FW: %d\n",devPath, ffuStatus);
            }
            break;

        case COMMAND_VERSION:
            printf("Getting version of %s FW\n", devPath);
            ret = GetVersion(devPath, fwVersion);
            printf("Get version of %s FW: %s\n", devPath, ret == 0 ? "Success" : "Failed");
            if (ret == 0)
            {
                printf("Version of %s FW: %s\n", devPath, fwVersion);
            }
            break;

        default:
            printf("Invalid command: %u\n", (uint32_t)command);
            break;
    }

    return ret;
}
