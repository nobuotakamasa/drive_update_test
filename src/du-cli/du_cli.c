/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>
#include "dus_client.h"

#ifdef __QNX__
#include "nvdtcommon.h"
#endif

// Defines the application return code
#define PASS  (0)
#define FAIL  (1)

#define FILE_NAME_LEN   (256)
#define SZ_2M           (0x200000UL)
#define SZ_4M           (0x400000UL)

/* payload */
static struct PAYLOAD_FILE
{
    char     payloadPath[FILE_NAME_LEN];
    uint8_t  payloadBuf[SZ_4M];
    uint32_t payloadFileLen;
    uint32_t partitionNameLen;
    char    *partitionName;
    uint32_t offsetLenBytes;
    uint64_t offset;
    uint32_t dataLen;
} payloadFile;

/* Totol task number need to be handled */
static uint32_t totalNum = 0;
/* Finished task number */
static uint32_t doneNum  = 0;

/* Define the suffix of the specified partition file */
static char l1ptSuffix[] = ".l1pt";
static char ptSuffix[] = ".pt";
static char payloadSuffix[] = ".patch";

/* Log buffer received from the DU server */
static char log[SZ_2M] = {0};

/* Start timestamp */
struct timespec start;
/* End timestamp */
struct timespec end;

#define B  (0x1UL)
#define KB (0x400UL)
#define MB (0x100000UL)
#define GB (0x40000000UL)

static const char *unitsString[] = {"B", "KB", "MB", "GB"};

static inline double convertSize(const double size, uint32_t *pUnitIndex)
{
    static const uint64_t units[] = {B, KB, MB, GB};
    uint64_t intPart = size;
    *pUnitIndex = 0;

    if(intPart >= KB && intPart < MB)
    {
        *pUnitIndex = 1;
    }
    else if(intPart < GB)
    {
        *pUnitIndex = 2;
    }
    else {
        *pUnitIndex = 3;
    }
    return size / units[*pUnitIndex];
}

static inline double ellapseTime(struct timespec *pStart, struct timespec *pEnd)
{
    double elapsed;
    // 1s = 1e9ns
    const uint64_t NANOSECOND_PER_SECOND = 1e9;
    elapsed = (double)(pEnd->tv_sec - pStart->tv_sec) +
              (double)(pEnd->tv_nsec - pStart->tv_nsec) / NANOSECOND_PER_SECOND;
    return elapsed;
}

static void traceLog(const char *func, int32_t line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    printf("%-15s: L%d ", func, line);
    vprintf(fmt, args);
    va_end(args);
}

#define dprintf(fmt, ...) traceLog(__func__, __LINE__, fmt, ##__VA_ARGS__)

static bool getTotalPayload(const char *dupkgDir)
{
    char filePath[FILE_NAME_LEN];
    uint32_t ptNum = 0;

    // First check if partition table, then check if a payload
    const static char *fileSuffix[] = {l1ptSuffix, ptSuffix, payloadSuffix};
    uint32_t suffixIndex = 0;
    uint32_t i = 1;

    while(suffixIndex < sizeof(fileSuffix) / sizeof(fileSuffix[0]))
    {
        snprintf(filePath, FILE_NAME_LEN, "%s/%d%s", dupkgDir, i, fileSuffix[suffixIndex]);
        // Check if file exists
        if(access(filePath, F_OK) != -1)
        {
            i++;
            totalNum++;
            // Caculate the number of the pt files.
            if(suffixIndex < 2U)
            {
                ptNum++;
            }
        }
        else
        {
            suffixIndex++;
        }
    }

    printf("Partition table: %u, Other payloads: %u, Total payloads: %u\n", ptNum, totalNum - ptNum, totalNum);
    return totalNum > 0;
}

static int32_t showPT(void)
{
    int32_t  ret      = PASS;
    uint32_t i        = 0;
    uint32_t ptLength = sizeof(log);

    dprintf("Get the PT info\n");
    ret = duscExecute(CMD_GET_PT_LOG, CHAIN_INVALID, NULL, 0, (uint8_t *)log, &ptLength);
    if(ret != DUSCLIENT_NO_ERROR)
    {
        dprintf("Error: duscExecute is failed\n");
        return ret;
    }

    for (i = 0; i < ptLength; i++)
    {
        printf("%c", log[i]);
    }
    return ret;
}

static int32_t getDUSLog(const char *dusLogFile)
{
    int32_t  fd            = -1;
    int32_t  writtenBytes  = 0;
    int32_t  ret           = PASS;
    uint32_t i             = 0;
    uint32_t logLength     = sizeof(log);

    dprintf("Get the server log %s\n", dusLogFile != NULL ? dusLogFile : "");
    ret = duscExecute(CMD_GET_SERVER_LOG, CHAIN_INVALID, NULL, 0, (uint8_t *)log, &logLength);
    if(ret != PASS)
    {
        dprintf("Error: sendAndReceive is failed\n");
        return FAIL;
    }

    if(dusLogFile != NULL)
    {
        fd = open(dusLogFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if(fd != -1)
        {
            writtenBytes = write(fd, log, logLength);
            if(writtenBytes == -1)
            {
                dprintf("Error: Failed to write %d bytes to the %s file\n", logLength, dusLogFile);
            }
            close(fd);
        }
    }
    if(fd == -1 || writtenBytes == -1 || dusLogFile == NULL)
    {
        dprintf("Print DUServer logs in the console.\n");
        for(i = 0; i < logLength; i++)
        {
            printf("%c", log[i]);
        }
    }
    return ret;
}

static int32_t parsePayload(uint8_t *pPayload)
{
    uint8_t *ptr  = NULL;
    uint8_t *pEnd = pPayload + payloadFile.payloadFileLen;
    uint32_t i    = 0;

    // NVDUPLD
    if(((DUS_PAYLOAD_HEADER *)pPayload)->magic != MAGIC_NVDUPLD)
    {
        dprintf("Error: Payload magic invalid = %#lx\n", ((DUS_PAYLOAD_HEADER *)pPayload)->magic);
        return FAIL;
    }

    /* Find out the partition name. */
    ptr = pPayload;
    ptr += sizeof(DUS_PAYLOAD_HEADER);
    if(ptr >= pEnd)
    {
        dprintf("Error: Get partitionNameLen failed: Out of boundary.\n");
        return FAIL;
    }
    // ptr -> partitionNameLen
    payloadFile.partitionNameLen = *(uint32_t *)ptr;

    ptr += sizeof(uint32_t);
    // ptr -> partitionName
    if(ptr >= pEnd)
    {
        dprintf("Error: Get partitionName failed: Out of boundary.\n");
        return FAIL;
    }
    payloadFile.partitionName = (char *)ptr;

    ptr += payloadFile.partitionNameLen;
    if(ptr >= pEnd)
    {
        dprintf("Error: Get offsetLenBytes failed: Out of boundary.\n");
        return FAIL;
    }
    // ptr -> offsetLenBytes
    payloadFile.offsetLenBytes = *((uint32_t *)ptr);

    ptr += sizeof(uint32_t);
    if(ptr + payloadFile.offsetLenBytes >= pEnd)
    {
        dprintf("Error: Get offset failed: Out of boundary.\n");
        return FAIL;
    }
    // ptr -> offset
    payloadFile.offset = 0;
    for(i = 0; i < payloadFile.offsetLenBytes; i++, ptr++)
    {
        payloadFile.offset = (payloadFile.offset << 8) | *ptr;
    }

    // ptr -> dataLen
    payloadFile.dataLen = *((uint32_t *)ptr);

    return PASS;
}

static inline int32_t displayProgress(uint64_t flashedBytes)
{
    uint32_t bytesIndex     = 0;
    uint32_t speedIndex     = 0;
    double   convertedBytes = 0.0;
    double   speedRate      = 0.0;

    if(clock_gettime(CLOCK_MONOTONIC, &end) == -1)
    {
        dprintf("Failed to clock_gettime()\n");
        return FAIL;
    }

    convertedBytes = convertSize(flashedBytes, &bytesIndex);
    speedRate = convertSize(flashedBytes / ellapseTime(&start, &end), &speedIndex);

    // Clear the line
    printf("\rProgress %6.2lf%% | %4u/%-4u | %.2lf%s | %.2lf %s/s @%.*s\033[K",
        doneNum * 100.0 / totalNum, doneNum, totalNum,
        convertedBytes, unitsString[bytesIndex],
        speedRate, unitsString[speedIndex],
        payloadFile.partitionNameLen, payloadFile.partitionName
    );
    return PASS;
}

#ifdef __QNX__
static uint8_t getActiveChain(void)
{
    uint8_t     ret = CHAIN_INVALID;
    nvdt_error  status = NVDT_SUCCESS;
    uint32_t    bootchain;
    void const *pNode = NULL;

    status = nvdt_open();
    if (status != NVDT_SUCCESS)
    {
        dprintf("nvdt_open() failed %d\n", status);
        return ret;
    }

    pNode = nvdt_get_node_by_path("/chosen/update-info");
    if (pNode == NULL)
    {
        dprintf("Failed to get update-info node\n");
        goto bailout;
    }

    status = nvdt_read_prop_array_by_index(pNode, "active-boot-chain",
        &bootchain, 1U, 0U);
    if (status != NVDT_SUCCESS)
    {
        dprintf("Failed to read active-boot-chain %d\n", status);
        goto bailout;
    }

    if(bootchain < (uint32_t)CHAIN_INVALID)
    {
        ret = (uint8_t)bootchain;
    }
    else
    {
        dprintf("Bootchain read %d is invalid\n", bootchain);
    }

bailout:
    nvdt_close();
    return ret;
}
#else
static uint8_t getActiveChain(void)
{
    uint8_t   ret = CHAIN_INVALID;
    FILE     *pFile;
    uint32_t  bootchain;
    size_t    len;

    pFile = fopen("/proc/device-tree/chosen/update-info/active-boot-chain", "rb");
    if (pFile == NULL)
    {
        dprintf("Failed to open device tree file\n");
        return ret;
    }

    len = fread(&bootchain, 1, sizeof(bootchain), pFile);

    if (len != sizeof(bootchain) || 0 != ferror(pFile))
    {
        dprintf("Error occurred while reading\n");
        goto bailout;
    }

    bootchain = ntohl(bootchain);
    if(bootchain < (uint32_t)CHAIN_INVALID)
    {
        ret = (uint8_t)bootchain;
    }
    else
    {
        dprintf("Bootchain read %d is invalid\n", bootchain);
    }

bailout:
    fclose(pFile);
    return ret;
}
#endif // __QNX__

static int32_t reloadPt(void)
{
    DUSCLIENT_ERROR ret = duscExecute(CMD_RELOAD_PT, 0, NULL, 0, NULL, NULL);
    return ret == DUSCLIENT_NO_ERROR ? PASS : FAIL;
}

static bool readFileContent(void)
{
    int32_t fd;
    int32_t ret = PASS;
    bool    ok  = true;
    struct stat st;
    fd = open(payloadFile.payloadPath, O_RDONLY);
    if(fd == -1)
    {
        return false;
    }

    ret = fstat(fd, &st);
    if(ret == -1)
    {
        dprintf("\nError: Failed to stat the file %s\n", payloadFile.payloadPath);
        ok = false;
        goto closeFile;
    }
    if((uint32_t)st.st_size <= sizeof(DUS_PAYLOAD_HEADER) ||
       (uint32_t)st.st_size > sizeof(payloadFile.payloadBuf))
    {
        dprintf("\nError: File size = %ld is too small or too big.\n", st.st_size);
        ok = false;
        goto closeFile;
    }
    payloadFile.payloadFileLen = st.st_size;

    ret = read(fd, payloadFile.payloadBuf, payloadFile.payloadFileLen);
    if(ret != (int32_t)payloadFile.payloadFileLen)
    {
        dprintf("\nError: Failed to read %u size from file %s\n",
            payloadFile.payloadFileLen, payloadFile.payloadPath);
        ok = false;
        goto closeFile;
    }

closeFile:
    close(fd);
    return ok;
}

static int32_t flashL1pt(void)
{
    int32_t ret = PASS;
    ret = parsePayload(payloadFile.payloadBuf);
    if(ret != PASS)
    {
        dprintf("Error: Failed to parsePayload\n");
        return FAIL;
    }

    ret = duscExecute(
        CMD_APPLY_PAYLOAD_PERSISTENT,
        CHAIN_PERSIST,
        payloadFile.payloadBuf,
        payloadFile.payloadFileLen,
        NULL,
        NULL
    );
    if(ret != DUSCLIENT_NO_ERROR)
    {
        dprintf("Failed to flash %.*s\n", payloadFile.partitionNameLen, payloadFile.partitionName);
        return FAIL;
    }
    return PASS;
}
static int32_t flashPayload(void)
{
    int32_t ret = PASS;
    uint8_t activeBootchain  = CHAIN_INVALID;
    uint8_t targetBootchain  = CHAIN_INVALID;
    ret = parsePayload(payloadFile.payloadBuf);
    if(ret != PASS)
    {
        dprintf("Error: Failed to parsePayload\n");
        return FAIL;
    }

    // If partitionName = "C_*", target chain = CHAIN_C
    // Else target chain = inactive chain
    if(payloadFile.partitionNameLen >= 2 &&
        payloadFile.partitionName[0] == 'C' &&
        payloadFile.partitionName[1] == '_')
    {
        targetBootchain = CHAIN_C;
    }
    else
    {
        activeBootchain = getActiveChain();
        if(activeBootchain == CHAIN_A)
        {
            targetBootchain = CHAIN_B;
        }
        else if((activeBootchain == CHAIN_B) || (activeBootchain == CHAIN_C))
        {
            targetBootchain = CHAIN_A;
        }
        else
        {
            dprintf("Failed to get target bootchain\n");
            return FAIL;
        }
    }

    ret = duscExecute(
        CMD_APPLY_PAYLOAD,
        targetBootchain,
        payloadFile.payloadBuf,
        payloadFile.payloadFileLen,
        NULL,
        NULL
    );
    if(ret != DUSCLIENT_NO_ERROR)
    {
        dprintf("Failed to flash %.*s\n", payloadFile.partitionNameLen, payloadFile.partitionName);
        return FAIL;
    }
    return PASS;
}
static int32_t deploy(const char *dupkgDir)
{
    int32_t  ret = PASS;
    uint32_t i   = 0U;
    const static char *fileSuffix[] = {l1ptSuffix, ptSuffix, payloadSuffix};
    uint32_t suffixIndex  = 0U;
    uint64_t flashedBytes = 0U;

    if(!dupkgDir || !getTotalPayload(dupkgDir))
    {
        // Payload num == 0 or dupkgDir == NULL
        dprintf("No payloads to be deployed\n");
        return PASS;
    }

    if(clock_gettime(CLOCK_MONOTONIC, &start) == -1)
    {
        dprintf("Error: Failed to clock_gettime()\n");
        return FAIL;
    }

    // Flash and update the partition table.
    for(i = 1U; i <= totalNum; i++)
    {
        /*
         * 1. Check partition table first, then check normal payload file.
         *    Check if i.pt exists first, if not, check if i.patch exists.
         * 2. If no i.pt or i.patch, end this deploy cycle.
         */
        while(suffixIndex < sizeof(fileSuffix) / sizeof(fileSuffix[0]))
        {
            snprintf(payloadFile.payloadPath, FILE_NAME_LEN, "%s/%d%s", dupkgDir, i, fileSuffix[suffixIndex]);
            if(!readFileContent())
            {
                suffixIndex++;
            }
            else
            {
                break;
            }
        }
        // No i.pt or i.patch, exit.
        if(suffixIndex >= sizeof(fileSuffix) / sizeof(fileSuffix[0]))
        {
            ret = FAIL;
            break;
        }

        if(suffixIndex == 0U)
        {
            ret = flashL1pt();
        }
        else
        {
            ret = flashPayload();
        }
        if(ret != PASS)
        {
            dprintf("\nFailed to flash %.*s\n", payloadFile.partitionNameLen, payloadFile.partitionName);
            break;
        }

        doneNum++;
        flashedBytes += payloadFile.dataLen;

        displayProgress(flashedBytes);

        // Flash the partition table, and execute reload pt command.
        if(suffixIndex == 0U || suffixIndex == 1U)
        {
            ret = reloadPt();
            printf("\nReloading partition table... ");
            if(ret != PASS)
            {
                dprintf("\nFailed to reload partition table in DUS, exit.\n");
                ret = FAIL;
                break;
            }
            printf("PASS\n");
        }
    }

    // Show the total time.
    printf("\nTotal time = %.2lf seconds\n", ellapseTime(&start, &end));

    return ret;
}

static void cleanUp(int32_t sigNum)
{
    if(duscDeinit() != DUSCLIENT_NO_ERROR)
    {
        dprintf("CleanUp: Error in duscDeinit\n");
    }

    dprintf("\nReceive the signal(%d). Exiting..\n", sigNum);
    exit(FAIL);
}

static void setupTerminationHandler(void)
{
    signal(SIGINT,  cleanUp);
    signal(SIGTERM, cleanUp);
    signal(SIGHUP,  cleanUp);
    signal(SIGQUIT, cleanUp);
    signal(SIGABRT, cleanUp);
}

static void help(void)
{
    printf("Options:\n");
    printf("\t -h: Print this help screen\n");
    printf("\t -d <dupkg_path> : directory of the OTA package\n");
    printf("\t -l <duserver_log_path> : Optional, the file to save duserver log\n");
}

static struct option long_options[] =
{
    {"help",        no_argument,        NULL,   'h'},
    {"deploy",      required_argument,  NULL,   'd'},
    {"logging",     required_argument,  NULL,   'l'},
    { NULL,         0,                  NULL,    0 }
};

static int32_t parseArgument(int32_t argc, char* argv[], char **pDupkgDir, char **pDusLogFile)
{
    int32_t ret          = PASS;
    int32_t opt          = 0;
    int32_t option_index = 0;

    while ((opt = getopt_long(argc, argv, "d:l:h", long_options, &option_index)) != -1)
    {
        switch (opt)
        {
            case 'd':
                if(optarg)
                {
                    *pDupkgDir = optarg;
                }
                else
                {
                    printf("-d: need a param for OTA package\n");
                    return FAIL;
                }
                break;
            case 'l':
                if(optarg)
                {
                    *pDusLogFile = optarg;
                }
                else
                {
                    printf("-l: need a param for saving DUServer log\n");
                    return FAIL;
                }
                break;
            case 'h':
                help();
            case '?':
                ret = FAIL;
                break;
            default:
                printf("Unknown option\n");
                break;
        }
    }

    if(optind < argc)
    {
        printf("Invalid options: ");
        while (optind < argc)
        {
            printf("%s ", argv[optind++]);
        }
        printf("\n");
        ret = FAIL;
    }
    return ret;
}

static void initSetting(void)
{
    // set unbuffered stdout output, no flushing required
    setvbuf(stdout, NULL, _IONBF, 0);
    // setup the tarmination handler
    setupTerminationHandler();
}

int32_t main(int32_t argc, char *argv[])
{
    int32_t ret         = PASS;
    char   *dupkgDir    = NULL;
    char   *dusLogFile  = NULL;

    ret = parseArgument(argc, argv, &dupkgDir, &dusLogFile);
    if(ret != PASS)
    {
        return ret;
    }

    initSetting();

    ret = duscInit();
    if(ret != DUSCLIENT_NO_ERROR)
    {
        dprintf("ERROR in duscInit, error = %#x!\n", ret);
        return FAIL;
    }

    ret = showPT();
    if(ret != DUSCLIENT_NO_ERROR)
    {
        dprintf("Failed to showPT.\n");
        goto checkDUSLog;
    }

    ret = deploy(dupkgDir);
    if(totalNum)
    {
        if(ret == PASS)
        {
            printf("Deploy SUCCESS!! You can switch to inactive chain now.\n");
        }
        else
        {
            printf("Deploy FAIL!!\n");
        }
    }

    /*
     * When DUS error received, or user request to get the log from DUS
     * Dump the DUS log
     */
checkDUSLog:
    if(ret == DUSCLIENT_ERROR_RECEIVE_DUSERVER_RETURN_ERROR || dusLogFile != NULL)
    {
        getDUSLog(dusLogFile);
    }

    ret = duscDeinit();
    if(ret != DUSCLIENT_NO_ERROR)
    {
        printf("Fail to duscDeinit, ret = %#x\n", ret);
    }

    return ret == 0 ? PASS : FAIL;
}