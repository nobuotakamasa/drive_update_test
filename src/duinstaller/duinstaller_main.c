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
 * @file  duinstaller_main.c
 * @brief Main entry point for Installer Plugin
 */

/* ------------------------ Drive Update Includes --------------------------- */
#include "duplugin.h"
#include "plugin_common/duinstaller_common.h"
#include "duinstaller.h"
#include "dulog.h"

/* ------------------------ System Includes --------------------------------- */
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>

/* ------------------------ Global Data Structures -------------------------- */
/// Global installer context
SAMPLE_INSTALLER gInstaller = {{0}};

static const char * const pHELP_MSG =
"DU Installer Plugin v1\n"
"\n"
"This program is intended to act as an example for developers writing \
installer plugins for Drive Update. DU Installer complies with the \
Drive Update installer plugin interface, and can perform a mock installation \
which may be replaced with a real software installation. Commands to the DU \
Installer should be sent via DU Master by placing commands in DU Master \
metadata. By default, this plugin will attempt to connect to DU Link router \
path at / and will have full path /plugin\n"
"\n"
"Usage:\n"
"./duinstaller [-h , --help]\n"
"\n"
"Commands supported by DU Installer:\n"
"\n"
"- deploy:\n"
"\t+ Required Args:\n"
"\t- path: DU Link Path of file to be installed\n"
"\t- savename (optional) Name to be used when saving file instead of the \
original filename\n"
"\t+ Behavior:\n"
"\tWill read file at input DU path and save the contents to the current \
installation directory. File will be saved as <filename>.staged until it has \
been committed. If file size is > max DU Link transport size, progress will be \
incrementally updated after each buffer chunk is sent.\n\n"
"- commit:\n"
"\t+ Required Args:\n"
"\tN/A\n"
"\t+ Behavior:\n"
"\tCommit a staged update. Only supported if state is PENDING_COMMIT. \
This will save the <filename>.staged file as <filename>, potentially \
overwriting any file with the same name\n";

/* ------------------------ Main Function ----------------------------------- */
int main(int argc, char* argv[])
{
    DU_RCODE duRet;
    int      opt;
    int      longIndex;

    static struct option longopts[] = {
        { "help",        no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    while ((opt = getopt_long(argc, argv, "h", longopts, &longIndex)) != -1)
    {
        switch (opt)
        {
            case 'h':
            {
                printf("%s", pHELP_MSG);
                exit(0);
                break;
            }
            default:
            {
                printf("Unexpected argument\n");
                printf("%s", pHELP_MSG);
                exit(1);
                break;
            }
        }
    }

    duRet = installerInit(&gInstaller);
    if (duRet != DU_OK)
    {
        DU_ERR("Installer initialization failed\n");
        goto bailout;
    }

    // Run forever unless fatal error occurs or SIGINT detected
    duRet = installerRun(&gInstaller);

bailout:
    return duRet;
}