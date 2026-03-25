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
 * @file   driveupdate.c
 * @brief  DRIVE Update V3 client application
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <libgen.h>
#include <getopt.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <termios.h>

/* ----------------------- Drive Update Includes -----------------------------*/
#include "dutransport.h"
#include "dulink.h"
#include "ducc.h"
#include "dujsonparser.h"
#include "duplugin.h"
#include "dulog.h"
#include "plugin_common/ducontent_provider_common.h"

/* ----------------------- Defines -------------------------------------------*/
#define MASTER_CMD           DUMASTER_PATH "/" NODE_CMD
#define CLIENT_NAME          "du-client"

#define TA_BOOTCHAIN_VER     "/tii/ver/active_bootchain"
#define TB_BOOTCHAIN_VER     "/gos-b/tii/ver/active_bootchain"

#define TA_PVIT              "/tii/A_pvit"
#define TB_PVIT              "/tii/B_pvit"
#define TC_PVIT              "/tii/C_pvit"

#define TA_TII_CMD_LOG       "/tii/cmd.log"
#define MASTER_PERS_LOG      "/master/persistent_log.list"

#define EXPORT_FILES         "files"
#define EXPORT_PUSH_CB       "push_cb"
#define TA_PUSH_MODE         "/tii/push_mode"
#define TA_PUSH_MODE_NOTIFY  "/tii/push_mode.notify"
/* the push_image_map.json has the mapping
for each image file to its corresponding
 partition exported by drive update plugin
*/
#define DU_PUSH_PARTS_MAP    "tii-a/push_image_map.json"
#define BOOTCHAIN_A_VER      "0"
#define BOOTCHAIN_B_VER      "1"
#define BOOTCHAIN_C_VER      "2"
#define A_BOOTCHAIN          "A"
#define B_BOOTCHAIN          "B"
#define C_BOOTCHAIN          "C"
#define DU_SLASH_CHAIN       "-chain"
#define DU_IMAGE             "image"
#define DU_PARTITION         "partition"
#define DU_TII_VALIDATE      "validate_p"
#define DU_TII_RPE_RELOAD    "reload"
#define DU_PUSHMODE_ENABLED    "enabled"
#define DU_PUSHMODE_DISABLED    "disabled"
#define DU_PUSHMODE_VALIDATING    "validating"

#define DU_MASTER_JSON       "du_master.json"
#define DU_AUTH_CONF_PATH    "/auth_conf.json"

#define DU_MASTER_CMD_ABORT  "abort"

#define INTERVAL_PROGRESS    (1U)
#define DUCC_MAX_RETRY       (60U)

enum command_type{
    COMMAND_HELP = 0,
    COMMAND_DEPLOY,
    COMMAND_ABORT,
    COMMAND_QUERY_BOOTCHAIN,
    COMMAND_GET_PART_VER,
    COMMAND_SET_RUNLEVEL,
    COMMAND_MAX = 0xFF
};
typedef enum command_type command_type;

// PVIT, refer bootloader/t234-staging-private/abi/mb2/tegrabl_pvit.h"
#define PVIT_MAX_ENTRIES    150U
#define PVIT_LOAD_OFFSET    (24UL * 1024UL)
#define UNIQUE_NAME_SIZE    26U
#define VERSION_SIZE        4U
#define SHA_SIZE            64U
#define PAD_1_SIZE          2U
#define PAD_2_SIZE          3U
#define BIN_SIZE            8U
#define TABLE_ID_SIZE       100U
#define RESERVED_SIZE       3U
#define TABLE_VERSION_SIZE  4u

struct __attribute__((__packed__)) pvit_rec {
    uint8_t unique_name[UNIQUE_NAME_SIZE];
    uint8_t pad1[PAD_1_SIZE];
    uint8_t version[VERSION_SIZE];
    uint8_t attributes;
    uint8_t pad2[PAD_2_SIZE];
    uint8_t bin_size[BIN_SIZE];
    uint8_t sha[SHA_SIZE];
};

struct __attribute__((__packed__)) pvit_header {
    uint8_t table_identifier[TABLE_ID_SIZE];
    uint8_t number_of_entries;
    uint8_t reserved[RESERVED_SIZE];
    uint8_t table_version[TABLE_VERSION_SIZE];
};
struct __attribute__((__packed__)) tegrabl_partition_version_info_table {
    struct pvit_header pvit_table_info;
    struct pvit_rec pvit_entry[PVIT_MAX_ENTRIES];
};

typedef struct tegrabl_partition_version_info_table pvit_t;

static char pvitBuf[DU_256KB];

// Max length of a single line print
#define DU_LOG_MAX_LEN       (2048U)

#define FILE_LIST_MAX_SIZE   (DU_64KB)
// Size equals to metadata max token * sizeof(jsmntok_t) = 10240 *16
#define JSON_ELEMENT_SIZE    (DU_1KB * 160U)

// Lock for validate in push mode
typedef enum {
    VALIDATE_PUSHMODE_FAIL       = 0U,
    VALIDATE_PUSHMODE_SUCCESS    = 1U,
    VALIDATE_PUSHMODE_INPROGRESS = 2U
} ValidatePushModeStatus;
static volatile ValidatePushModeStatus validatePushModeStatus = VALIDATE_PUSHMODE_FAIL;
static pthread_mutex_t validatePushModeMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t validatePushModeCond = PTHREAD_COND_INITIALIZER;

/* ----------------------- Static variables ----------------------------------*/
static DUTR_NVSCI_PARAM        dutrCCParams = \
    { .ep[0] = "nvdu_gos_ipc_i_1", .ep[1] = "nvdu_gos_ipc_i_0"};
static DUTR_SEC_NONE_PARAM     dutrCCSecParams;
static CONTENT_PROVIDER_COMMON content_provider;

static uint32_t  gRefId = DULINK_CLOSE_ALL;
static PDUCC     gpDucc = NULL;
static bool      gbRmCbPath = false;
static char      gParentPath[DULINK_MAX_PATH] = {0};
static bool      quit;
static bool      bPrintStatus = true;
static char      buffer[DULINK_MAX_DATA_SIZE] = {0};
static bool      bEnableDelta = false;

static struct option long_options[] =
{
    {"help",              no_argument,       NULL,        'h'},
    {"package",           required_argument, NULL,        'p'},
    {"abort",             no_argument,       NULL,        'a'},
    {"auth",              no_argument,       NULL,        't'},
    {"query_bootchain",   no_argument,       NULL,        'q'},
    {"part_ver",          no_argument,       NULL,        'g'},
    {"get_runlevel",      no_argument,       NULL,        'e'},
    {"set_runlevel",      required_argument, NULL,        's'},
    {"print_status",      required_argument, NULL,        'd'},
    {"du_read",           required_argument, NULL,        'r'},
    {"du_write",          required_argument, NULL,        'w'},
    {"du_list",           required_argument, NULL,        'l'},
// flags
    {"push",              no_argument,       NULL,        'u'},
    {"delta",             no_argument,       NULL,        'd'},
    {"target-controlled", no_argument,       NULL,        'c'},
    {NULL,                0,                 NULL,          0}
};

#define ARRAY_SIZE(a)    (sizeof(a) / sizeof((a)[0]))
struct cmd
{
    const char *cmd;
    const char *helpMsg;
    DU_RCODE (*func)(int argc, const char *argv[]);
};

static DU_RCODE usage(int argc, const char **argv);
static DU_RCODE deployPackage(int argc, const char *argv[]);
static DU_RCODE abortDeploy(int argc, const char *argv[]);
static DU_RCODE queryBootchain(int argc, const char *argv[]);
static DU_RCODE getPartVer(int argc, const char *argv[]);
static DU_RCODE leave(int argc, const char *argv[]);
static DU_RCODE getRunlevel(int argc, const char *argv[]);
static DU_RCODE setRunlevel(int argc, const char *argv[]);
static DU_RCODE printStatus(int argc, const char *argv[]);
static DU_RCODE duRead(int argc, const char *argv[]);
static DU_RCODE duWrite(int argc, const char *argv[]);
static DU_RCODE duList(int argc, const char *argv[]);
static DU_RCODE sys(int argc, const char *argv[]);

static struct cmd gCmds[] =
{
    {
        "help",
        "help(--help, -h)\n"
        "\t\tPrint this help info",
        usage
    },
    {
        "shell",
        "shell <cmdline>\n"
        "\t\tExecute shell command",
        sys
    },
    {
        "package",
        "package(--package, -p) <DUPKG> [options]\n"
        "\t\tTrigger installation of particular DUPKG\n"
        "\t\tThe DUPKG should be already present in DULINK space\n"
        "\t\tBy DUPKG specification, du_master.json should be present directly under given dulink path\n"
        "\t\tThis parameter can be used with remote_content_privoder to use their functionality to host DUPKG\n"
        "\t\tExample usage: package /content/files\n"
        "\t    -t, --auth\n"
        "\t\tForce check authenticate packages\n"
        "\t    -u, --push\n"
        "\t\tThis flag indicates this update would run in push mode\n"
        "\t\tIn push mode, driveupdate would actively write to storage\n"
        "\t\t[NOTE]driveupdate only support push updates for TA\n"
        "\t    -c, --target-controlled\n"
        "\t\tThis flag indicates this update would interact with user\n"
        "\t\tUser will explicitly type \"y\" to raise run level\n"
        "\t    -d, --delta\n"
        "\t\tThis flag indicates a delta-push update using bsdiff as delta algorithm",
        deployPackage
    },
    {
        "abort",
        "abort(-a)\n"
        "\t\tForce abort any on-going installation",
        abortDeploy
    },
    {
        "query_bootchain",
        "query_bootchain(-q)\n"
        "\t\tQuery which bootchain either Tegra is on\n"
        "\t\tA is Chain-A and B is Chain-B",
        queryBootchain
    },
    {
        "part_ver",
        "part_ver(--part_ver, -g)\n"
        "\t\tPrint all the partitions version in PVIT",
        getPartVer
    },
    {
        "get_runlevel",
        "get_runlevel(--get_runlevel, -e)\n"
        "\t\tGet Current Runlevel\n"
        "\t\tExample usage: get_runlevel",
        getRunlevel,
    },
    {
        "set_runlevel",
        "set_runlevel(--set_runlevel, -s) <runlevel>\n"
        "\t\tSet Runlevel from 0 to 6\n"
        "\t\tExample usage: set_runlevel 3",
        setRunlevel,
    },
    {
        "print_status",
        "print_status(--print_status, -d) on/off\n"
        "\t\tEnable/Disable regular deployment status print\n"
        "\t\t[NOTE] Command only be supported in interaction mode\n"
        "\t\tExample usage: print_status on",
        printStatus,
    },
    {
        "du_read",
        "du_read(--du_read, -r) src dst(optional)\n"
        "\t\tdulinkRead a node to a local file(dst)\n"
        "\t\tExample usage: du_read /content/node\n"
        "\t\tExample usage: du_read /content/node /home/nvidia/file",
        duRead,
    },
    {
        "du_write",
        "du_write(--du_write, -w) data dst\n"
        "\t\tdulinkWrite data to node(dst)\n"
        "\t\tExample usage: du_write abc /content/node",
        duWrite,
    },
    {
        "du_list",
        "du_list(--du_list, -l) directory\n"
        "\t\tList the nodes in directory\n"
        "\t\tExample usage: du_list /content",
        duList,
    },
    {
        "exit",
        "exit\n"
        "\t\tExit sample",
        leave
    },
};

static char * const DUCC_STATE_TABLE[NUM_DUCC_STATES] =
{
    [STATE_DORMANT]            = "STATE_DORMANT",
    [STATE_NO_CONNECTIVITY]    = "STATE_NO_CONNECTIVITY",
    [STATE_UP_TO_DATE]         = "STATE_UP_TO_DATE",
    [STATE_UPDATE_AVAILABLE]   = "STATE_UPDATE_AVAILABLE",
    [STATE_UPDATE_IN_PROGRESS] = "STATE_UPDATE_IN_PROGRESS",
    [STATE_UPDATE_FAILED]      = "STATE_UPDATE_FAILED",
    [STATE_FATAL_ERROR]        = "STATE_FATAL_ERROR",
};

/* ------------------------ DU Link Callback Contexts ----------------------- */
// Contexts for read only string callbacks
static CTX_LOCK_STR ctxPluginType =
{
    .pStr = PLUGIN_TYPE_CONTENT_PROVIDER,
    .pMutex = NULL
};

static CTX_LOCK_STR ctxState =
{
    .pStr = content_provider.stateStr,
    .pMutex = &content_provider.mutex
};

// Contexts for runlevels
static CTX_LOCK_RL ctxCurrentRl =
{
    .pRl = &content_provider.currentRl,
    .pMutex = &content_provider.mutex
};

static CTX_LOCK_RL ctxPendingRl =
{
    .pRl = &content_provider.pendingRl,
    .pMutex = &content_provider.mutex
};

// Contexts for .list nodes
static CTX_LIST_STR ctxStateList =
{
    .size = CONTENT_STATE_MAX,
    .ppStrList = (char const **) CONTENT_STATES_TABLE
};

/* ------------------------ DU Link Node Definitions ------------------------ */
/// Table containing all file nodes to export
#define DULINK_FILES_TABLE_MAX (7)
static const DULINK_EXPORT_REQS DULINK_FILES_TABLE[DULINK_FILES_TABLE_MAX] =
{
    // plugin-type
    { .pPath = NODE_PLUGIN_TYPE, .attr = READ_ONLY_ATTR,
      .cb = readOnlyStringCB, .pCtx = &ctxPluginType },
    // requested_rl
    { .pPath = NODE_REQUESTED_RL, .attr = MASTER_ONLY_RW_ATTRIBUTE,
      .cb = contentRequestedRlCB, .pCtx = &content_provider },
    // current_rl
    { .pPath = NODE_CURRENT_RL, .attr = READ_ONLY_ATTR,
      .cb = readOnlyRunlevelCB, .pCtx = &ctxCurrentRl },
    // persistent_ctx_path
    { .pPath = NODE_PERSISTENT_CTX_PATH, .attr = READ_WRITE_ATTRIBUTE,
      .cb = contentPersistCtxCBPlugin, .pCtx = &content_provider },
    // pending_rl
    { .pPath = NODE_PENDING_RL, .attr = READ_ONLY_ATTR,
      .cb = readOnlyRunlevelCB, .pCtx = &ctxPendingRl },
    // state
    { .pPath = NODE_STATE, .attr = READ_ONLY_ATTR,
      .cb = readOnlyStringCB, .pCtx = &ctxState },
    // state.list
    { .pPath = NODE_STATE_LIST, .attr = READ_ONLY_ATTR,
      .cb = listNodeCB, .pCtx = &ctxStateList },
};

/// Table containing all .notify nodes to export
#define DULINK_NOTIFY_TABLE_MAX (3)
static const DULINK_EXPORT_REQS DULINK_NOTIFY_TABLE[DULINK_NOTIFY_TABLE_MAX] =
{
    // current_rl.notify
    { .pPath = NOTIFY_NODE_CURRENT_RL },
    // pending_rl.notify
    { .pPath = NOTIFY_NODE_PENDING_RL },
    // state.notify
    { .pPath = NOTIFY_NODE_STATE },
};

/* ----------------------- Static functions ----------------------------------*/
/*!
 * @brief Block signal to make sure atomic critical section
 */
static void blockSig(void)
{
    sigset_t sigset;
    (void) sigemptyset(&sigset);
    (void) sigaddset(&sigset, SIGINT);
    (void) sigaddset(&sigset, SIGTERM);
    (void) sigaddset(&sigset, SIGQUIT);
    (void) sigprocmask(SIG_BLOCK, &sigset, NULL);
}

/*!
 * @brief Unblock signal
 */
static void unBlockSig(void)
{
    sigset_t sigset;
    (void) sigemptyset(&sigset);
    (void) sigaddset(&sigset, SIGINT);
    (void) sigaddset(&sigset, SIGTERM);
    (void) sigaddset(&sigset, SIGQUIT);
    (void) sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}

/*!
 * @brief Initialize DUCC connection
 */
static DU_RCODE initDuccConn(void)
{
    DU_RCODE     duRet = DU_OK;
    DU_RUN_LEVEL runlevel = RL_INVALID;
    uint32_t     sleepSec = 1U;
    uint32_t     retry = 0U;

    duRet = DUCC_Init(DUTR_TR_TYPE_NVSCI, DUTR_SEC_TYPE_NONE,
                      &dutrCCParams, &dutrCCSecParams, &gpDucc);
    if (duRet != DU_OK)
    {
        DU_ERR("Error initialize C&C, err:%#x\n", duRet);
        goto end;
    }

    for (retry = 1U; retry <= DUCC_MAX_RETRY; retry++)
    {
        duRet = DUCC_Get_Current_RunLevel(gpDucc, &runlevel);
        if (duRet == DU_OK)
        {
            break;
        }
        else
        {
            DU_INFO("ret:%#x on DUCC_Get_Current_RunLevel()\n", duRet);
            if (retry < DUCC_MAX_RETRY)
            {
                DU_INFO("DUCC is not ready, sleep %us and retry\n", sleepSec);
                (void) sleep(sleepSec);
            }
            else
            {
                DU_ERR("Error fetching runlevel, make sure app is running"
                    " on master Tegra!\n");
                duRet = CONTENT_ERR_GENERIC;
            }
        }
    }

end:
    return duRet;
}

/*!
 * @brief Initialize DULINK connection
 */
static DU_RCODE initDulinkConn(void)
{
    DULINK_CONNECT_INFO duConnInfo = {0};

    if (getPluginConnInfo(CLIENT_NAME, &duConnInfo) != DU_OK)
    {
        DU_ERR("Failed to get connection info\n");
        return CONTENT_ERR_GENERIC;
    }

    if (getPluginParent(gParentPath, DULINK_MAX_PATH) != DU_OK)
    {
        DU_ERR("Failed to fetch parentPath\n");
        return CONTENT_ERR_GENERIC;
    }

    if (dulinkInit(CLIENT_NAME, duConnInfo.remotePath) != DU_OK)
    {
        DU_ERR("Failed to init DULINK\n");
        return CONTENT_ERR_GENERIC;
    }

    if (exportDULinkNodes(NULL, 0, DULINK_FILES_TABLE, DULINK_FILES_TABLE_MAX,
            DULINK_NOTIFY_TABLE, DULINK_NOTIFY_TABLE_MAX) != DU_OK)
    {
        DU_ERR("Failed to export all DU Link nodes\n");
        return CONTENT_ERR_GENERIC;
    }

    initContentProvider(&content_provider);

    if (dulinkOpen(duConnInfo.remotePath, duConnInfo.trType, duConnInfo.seType,
                    (PDUTR_TR_PARAM) &duConnInfo.trParamBuf,
                    (PDUTR_SEC_PARAM) &duConnInfo.secParamBuf,
                    duConnInfo.connType, &gRefId) != DU_OK)
    {
        DU_ERR("Failed to connect DULINK\n");
        return CONTENT_ERR_GENERIC;
    }

    if (registerToMaster(DUMASTER_PATH) != DU_OK)
    {
        DU_ERR("Failed to register to DU Master\n");
        return CONTENT_ERR_GENERIC;
    }

    return DU_OK;
}

/*!
 * @brief Clean up connections
 */
static void cleanUpConn(void)
{
    char        cmdBuf[DU_STR_SHORT_BUF_SIZE] = {0};
    const char *pPath = DUMASTER_PATH "/plugins";
    uint64_t    retLen;

    if (gRefId != DULINK_CLOSE_ALL)
    {
        (void) strlcpy(cmdBuf, "deregister", DU_STR_SHORT_BUF_SIZE);
        if (dulinkWrite(pPath, 0, strlen(cmdBuf)+1U, cmdBuf, &retLen) != DU_OK)
        {
            DU_ERR("Can't deregister from master\n");
        }

        if (gbRmCbPath)
        {
            retLen = snprintf(cmdBuf, sizeof(cmdBuf), "-%s%s/%s",
                    gParentPath, CLIENT_NAME, EXPORT_PUSH_CB);
            if (retLen >= (int32_t) sizeof(cmdBuf))
            {
                DU_ERR("%s is truncated to %lu\n", cmdBuf, retLen);
            }
            else
            {
                if (dulinkWrite(TA_PUSH_MODE_NOTIFY, 0, strlen(cmdBuf)+1U,
                    cmdBuf, &retLen) != DU_OK)
                {
                    DU_WARN("Error on unregister %s to %s\n", cmdBuf,
                            TA_PUSH_MODE_NOTIFY);
                }
            }

            if (dulinkUnlinkFile(EXPORT_PUSH_CB) != DU_OK)
            {
                DU_WARN("Error on dulinkUnlinkFile %s\n", EXPORT_PUSH_CB);
            }
            gbRmCbPath = false;
        }

        if (dulinkClose(gRefId) != DU_OK)
        {
            DU_ERR("Error disconnect DU LINK\n");
        }
    }

    if (gpDucc != NULL)
    {
        if (DUCC_Close(gpDucc) != DU_OK)
        {
            DU_ERR("Error stop DUCC\n");
        }
    }
}
/* ----------------------- COMMAND_HELP --------------------------------------*/
/*!
 * @brief Print the usage of this tool
 */
static DU_RCODE usage(int argc, const char **argv)
{
    DU_LOG("Nvidia Drive Update client"
           "\nUsage:"
           "\nOperations:");

    for (size_t i = 0; i < ARRAY_SIZE(gCmds); i++)
    {
        DU_LOG("\n\n\t%s", gCmds[i].helpMsg);
    }

    DU_LOG("\n");
    return DU_OK;
}

/* ----------------------- Host file -----------------------------------------*/
/*!
 * @brief Check if auth package exist
 */
static DU_RCODE
checkAuthPackage(const char *pPkgPath)
{
    DU_RCODE duRet;
    char     authConfPath[DU_PATH_MAX] = {0};
    size_t   fullPathSize = 0U;
    uint64_t duSize = 0U;

    fullPathSize = strlen(pPkgPath) + strlen(DU_AUTH_CONF_PATH) + 1U;
    if (fullPathSize > sizeof(authConfPath))
    {
        duRet = CONTENT_ERR_BUFFER_TOO_SMALL;
        DU_ERR("Path buffer is too small, need %lu at least\n", fullPathSize);
        goto exit;
    }

    (void)strlcpy(authConfPath, pPkgPath, sizeof(authConfPath));
    (void)strncat(authConfPath, DU_AUTH_CONF_PATH,
                  (sizeof(authConfPath) - strlen(authConfPath) - 1U));
    // check if auth_conf.json exists and has read permission in the package
    duRet = dulinkGetSize(authConfPath, &duSize);

    if (duRet == DU_OK)
    {
        DU_INFO("Auth DU Package: auth_conf.json is found\n");
    }
    else
    {
        DU_INFO("Auth DU Package: auth_conf.json is not found\n");
        duRet = CONTENT_ERR_NOT_FOUND;
    }

exit:
    return duRet;
}

/* ----------------------- COMMAND_DEPLOY ------------------------------------*/
/*!
 * @brief Update/Clean persistent context
 */
static DU_RCODE
updateContext(const char *pPkgPath, bool bPush)
{
    char context[DU_1KB] = {0};
    uint64_t retLen;

    if (pPkgPath != NULL)
    {
        snprintf(context, sizeof(context),
        "{\"pkgPath\":\"%s\",\"bPush\":%d}",
            pPkgPath, bPush ? 1 : 0);
    }

    if (dulinkWrite(content_provider.persistCtxPath, 0,
                    strlen(context), context, &retLen) != DU_OK)
    {
        // we just ignore the fail
        DU_WARN("Can't Updating ctx, deploy won't be resumed across reboot\n");
    }
    return DU_OK;
}

/*!
 * @brief perform a push Update
 */
static void*
performPush(void *pArg)
{
    int32_t         mapIndex;
    uint64_t        retLen;
    uint64_t        readlen;
    DU_RCODE        duRet = DU_OK;
    char            activeBootChain[DU_STR_SHORT_BUF_SIZE];
    char            inactiveChain[DU_STR_SHORT_BUF_SIZE];
    static char     elmtArray[JSON_ELEMENT_SIZE];
    json_t          *pJson;
    jsonElement_t   *pJsonRootElmt;
    jsonElement_t   *pJsonInacChainElmt;
    jsonElement_t   *pJsonTmpElmt;
    int32_t         jsonLen = 0;
    int32_t         jsonArraySize = 0;
    uint64_t        offset;
    char            imageName[DU_PATH_MAX];
    char            partitionName[DU_PATH_MAX];
    char            fullName[DU_PATH_MAX] = {0};
    static char     jsonBuf[FILE_LIST_MAX_SIZE];
    char            pushmode_enabled[] = DU_PUSHMODE_ENABLED;
    char            pushmode_disabled[] = DU_PUSHMODE_DISABLED;
    char            pushmode_validating[] = DU_PUSHMODE_VALIDATING;
    static char     pushFileBuf[DU_1MB];
    char            *pPkgPath = (char *) pArg;
    int32_t         len;
    char            jsonStrBuf[DU_1KB] = {0};
    int32_t         ret;
    char            abortCmd[DU_STR_LONG_BUF_SIZE] = DU_MASTER_CMD_ABORT;

    // Get active boot chain
    duRet = dulinkRead(TA_BOOTCHAIN_VER, 0, DU_STR_SHORT_BUF_SIZE,
            activeBootChain, &retLen);
    if (duRet != DU_OK)
    {
        DU_ERR("Error %#x on dulinkRead %s\n", duRet, TA_BOOTCHAIN_VER);
        goto out;
    }

    if (strcmp(activeBootChain, BOOTCHAIN_A_VER) == 0)
    {
        (void) strlcpy(inactiveChain, B_BOOTCHAIN DU_SLASH_CHAIN, sizeof(inactiveChain));
    }
    else if (strcmp(activeBootChain, BOOTCHAIN_B_VER) == 0)
    {
        (void) strlcpy(inactiveChain, A_BOOTCHAIN DU_SLASH_CHAIN, sizeof(inactiveChain));
    }
    else
    {
        DU_ERR("%s is not a valid bootchain\n", activeBootChain);
        duRet = CONTENT_ERR_GENERIC;
        goto out;
    }

    // Open partition mapping file to get all images files
    snprintf(fullName, DU_PATH_MAX, "%s/%s", pPkgPath, DU_PUSH_PARTS_MAP);
    DU_LOG("Reading image mapping from %s\n", fullName);

    duRet = dulinkRead(fullName, 0, sizeof(jsonBuf), jsonBuf, &retLen);
    if (duRet != DU_OK)
    {
        DU_ERR("Error %#x on dulinkRead %s\n", duRet, fullName);
        goto out;
    }

    // Parser JSON file
    pJson = jsonParser(jsonBuf, sizeof(jsonBuf), elmtArray, JSON_ELEMENT_SIZE);
    if (pJson == NULL)
    {
        DU_ERR("Error on jsonParser\n");
        duRet = CONTENT_ERR_NOT_FOUND;
        goto out;
    }

    // Get root element
    pJsonRootElmt = jsonGetRoot(pJson);
    if (pJsonRootElmt == NULL)
    {
        DU_ERR("Error on jsonGetRoot\n");
        duRet = CONTENT_ERR_NOT_FOUND;
        goto out;
    }

    // get inactive bootchain image list
    pJsonInacChainElmt = jsonGetByName(pJsonRootElmt, inactiveChain);
    if (pJsonInacChainElmt == NULL)
    {
        DU_ERR("Error on jsonGetByName %s\n", inactiveChain);
        duRet = CONTENT_ERR_NOT_FOUND;
        goto out;
    }

    jsonArraySize = jsonGetArraySize(pJsonInacChainElmt);

    if (jsonGetType(pJsonInacChainElmt) != JSON_ARRAY)
    {
        DU_ERR("Bad format of %s section in metadata.\n", inactiveChain);
        duRet = CONTENT_ERR_GENERIC;
        goto out;
    }

    DU_INFO("Total %d images.\n", jsonArraySize);
    // Write and validate partition one by one
    for (mapIndex = 0; mapIndex < jsonArraySize; mapIndex++)
    {
        // get image name and partition name from JSON token
        pJsonTmpElmt = jsonGetByIndex(pJsonInacChainElmt, mapIndex);
        if (pJsonTmpElmt == NULL)
        {
            DU_ERR("#Error on jsonGetByIndex\n");
            duRet = CONTENT_ERR_NOT_FOUND;
            goto out;
        }

        // get image name
        jsonLen = jsonGetStrByName(pJsonTmpElmt, DU_IMAGE,
                                    imageName, sizeof(imageName));
        if (jsonLen == 0)
        {
            // check if it is validate
            jsonLen = jsonGetStrByName(pJsonTmpElmt, DU_TII_VALIDATE,
                                         partitionName, sizeof(partitionName));
            if (jsonLen != 0)
            {
                DU_DBG("Validate on push mode\n");
                // dulinkwrite to push_mode and let tii start validate
                duRet = dulinkWrite(TA_PUSH_MODE, 0,
                                    strlen(DU_PUSHMODE_VALIDATING),
                                    (void *)pushmode_validating, &retLen);
                if (duRet != DU_OK)
                {
                    DU_ERR("Error %#x on dulinkWrite %s to %s", duRet,
                           imageName, TA_PUSH_MODE);
                    goto out;
                }
                // Set validate push mode to VALIDATE_PUSHMODE_INPROGRESS.
                validatePushModeStatus = VALIDATE_PUSHMODE_INPROGRESS;

                // Get and Send json string to tii for validating
                jsonLen = jsonPrintElement(pJsonTmpElmt, jsonStrBuf, DU_1KB);
                if (jsonLen < 0)
                {
                    DU_ERR("Get %d element string failed.\n", mapIndex);
                    duRet = CONTENT_ERR_NOT_FOUND;
                    goto out;
                }

                duRet = dulinkWrite(partitionName, 0, (uint64_t)jsonLen,
                                    jsonStrBuf, &retLen);
                if (duRet != DU_OK)
                {
                    DU_ERR("Error %#x on dulinkWrite %s to %s", duRet,
                           imageName, partitionName);
                    goto out;
                }

                // condWait here until validate finished
                ret = pthread_mutex_lock(&validatePushModeMutex);
                if (ret != 0)
                {
                    DU_ERR("Mutex lock failed, (%s)\n", strerror(ret));
                    duRet = CONTENT_ERR_GENERIC;
                    goto out;
                }
                while (validatePushModeStatus == VALIDATE_PUSHMODE_INPROGRESS)
                {
                    ret = pthread_cond_wait(&validatePushModeCond,
                                            &validatePushModeMutex);
                    if (ret != 0)
                    {
                        DU_ERR("Condvar wait failed, (%s)\n", strerror(ret));
                        duRet = CONTENT_ERR_GENERIC;
                        goto out;
                    }
                }
                ret = pthread_mutex_unlock(&validatePushModeMutex);
                if (ret != 0)
                {
                    DU_ERR("Mutex unlock failed, (%s)\n", strerror(ret));
                    duRet = CONTENT_ERR_GENERIC;
                    goto out;
                }

                if (validatePushModeStatus == VALIDATE_PUSHMODE_SUCCESS)
                {
                    DU_DBG("Validate success in push mode\n");
                    // set push mode to enabled after validate success
                    duRet = dulinkWrite(TA_PUSH_MODE, 0,
                                        strlen(DU_PUSHMODE_ENABLED),
                                        (void *)pushmode_enabled, &retLen);
                    if (duRet != DU_OK)
                    {
                        DU_ERR("Error %#x on dulinkWrite %s to %s", duRet,
                                imageName, TA_PUSH_MODE);
                        goto out;
                    }
                    continue;
                }
                else
                {
                    DU_ERR("Validate fail in push mode\n");
                    duRet = CONTENT_ERR_GENERIC;
                    goto out;
                }

            }
            else
            {
                // Check if it is reload
                jsonLen = jsonGetStrByName(pJsonTmpElmt, DU_TII_RPE_RELOAD,
                                            imageName, sizeof(imageName));
                if (jsonLen == 0)
                {
                    DU_ERR("Error on load %s\n", DU_TII_RPE_RELOAD);
                    duRet = CONTENT_ERR_NOT_FOUND;
                    goto out;
                }

                // send the reload command
                DU_INFO("Send cmd %s to %s\n", DU_TII_RPE_RELOAD, imageName);
                duRet = dulinkWrite(imageName, 0, strlen(DU_TII_RPE_RELOAD),
                                DU_TII_RPE_RELOAD, &retLen);
                if (duRet != DU_OK)
                {
                    DU_ERR("Error %#x on dulinkWrite %s to %s", duRet,
                           imageName, DU_TII_RPE_RELOAD);
                    goto out;
                }

                continue;
            }
        }

        // get partition name
        jsonLen = jsonGetStrByName(pJsonTmpElmt, DU_PARTITION,
                                    partitionName, sizeof(partitionName));
        if (jsonLen == 0)
        {
            DU_ERR("Error on load %s\n", DU_PARTITION);
            duRet = CONTENT_ERR_NOT_FOUND;
            goto out;
        }

        (void) memset(fullName, 0, DU_PATH_MAX);
        len = snprintf(fullName, DU_PATH_MAX, "%s/%s", pPkgPath, imageName);
        if (len >= (int32_t) DU_PATH_MAX)
        {
            DU_ERR("%s is truncated to %d\n", fullName, len);
            duRet = CONTENT_ERR_GENERIC;
            goto out;
        }
        DU_LOG("Image %4d: %s, partition: %s\n", mapIndex, fullName,
                partitionName);

        // write partition
        offset = 0U;
        do
        {

            if (bEnableDelta == true)
            {
                DU_DBG("Attempting to write %lu \n", retLen);
                duRet = dulinkWrite(imageName, offset,
                                    sizeof(pushFileBuf), pushFileBuf, &retLen);
                if (duRet != DU_OK)
                {
                    DU_ERR("Error %#x on dulinkWrite %s\n", duRet, fullName);
                    goto out;
                }
                offset += retLen;
            }
            else
            {
                duRet = dulinkRead(fullName, offset,
                                    sizeof(pushFileBuf), pushFileBuf, &retLen);
                if (duRet != DU_OK)
                {
                    DU_ERR("Error %#x on dulinkRead %s\n", duRet, fullName);
                    goto out;
                }

                // save the read buffer length
                readlen = retLen;

                if (retLen != 0U)
                {
                    // write partition
                    duRet = dulinkWrite(partitionName, offset, retLen, pushFileBuf,
                            &retLen);

                    // If write return length is not equal to read buffer length,
                    // this means not all the data is written properly,
                    // we should fail out here.
                    if (readlen != retLen)
                    {
                        DU_ERR("fail to write all the data\n");
                        DU_ERR("Read out data: offset: %#lx, length: %#lx \n", offset, readlen);
                        DU_ERR("Written  data: offset: %#lx, length: %#lx \n", offset, retLen);
                        duRet = DULINK_ERR_GENERIC;
                        goto out;
                    }
                    if (duRet != DU_OK)
                    {
                        DU_ERR("Error %#x on dulinkWrite %s", duRet, partitionName);
                        goto out;
                    }
                    offset += retLen;
                }
            }
        } while (retLen == sizeof(pushFileBuf));
    }

    DU_INFO("%d images are written\n", mapIndex);

out:
    if (duRet == DU_OK)
    {
        // Notify DU-TII to disable push mode
        DU_LOG("All write and validate are successful, disable push mode\n");
        duRet = dulinkWrite(TA_PUSH_MODE, 0, strlen(DU_PUSHMODE_DISABLED),
                (void *)pushmode_disabled, &retLen);
        if (duRet != DU_OK)
        {
            DU_ERR("%u on Write %s\n", duRet, TA_PUSH_MODE);
        }
    }
    else
    {
        // When error, abort the deployment
        DU_LOG("Fail at write or validate, abort the deployment\n");
        duRet = dulinkWrite(MASTER_CMD, 0, strlen(abortCmd),
                            (void *) abortCmd, &retLen);
        if (duRet != DU_OK)
        {
            DU_ERR("%u on abort\n", duRet);
        }
    }

    return NULL;
}

/*!
 * @brief Callback to be triggered when push mode is enabled
 */
static DU_RCODE pushCB
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
    DU_RCODE   duRet = DU_OK;
    int32_t    ret;
    char      *pPkgPath = (char *) pCtx;
    pthread_t  threadId;

    *pRetVal = 0;

    if (offset != 0)
    {
        DU_ERR("Invalid offset to cmd\n");
        duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
        goto bailout;
    }

    switch (operation)
    {
        case DULINK_CB_WRITE:
        {
            if (strcmp(pBuf, "enabled") == 0)
            {
                DU_INFO("Start push update!\n");
                if (pthread_create(&threadId, NULL, performPush, pPkgPath) != 0)
                {
                    DU_ERR("Error on pthread_create\n");
                }
                (void) pthread_detach(threadId);
            }
            if (strcmp(pBuf, "validateSuccess") == 0)
            {
                DU_DBG("Tii validate success!\n");
                validatePushModeStatus = VALIDATE_PUSHMODE_SUCCESS;
                ret = pthread_cond_signal(&validatePushModeCond);
                if (ret != 0)
                {
                    DU_ERR("Condvar signal failed, (%s)\n", strerror(ret));
                    goto bailout;
                }
            }
            if (strcmp(pBuf, "validateFail") == 0)
            {
                DU_ERR("Tii validate fail\n");
                validatePushModeStatus = VALIDATE_PUSHMODE_FAIL;
                ret = pthread_cond_signal(&validatePushModeCond);
                if (ret != 0)
                {
                    DU_ERR("Condvar signal failed, (%s)\n", strerror(ret));
                    goto bailout;
                }
            }
            break;
        }
        case DULINK_CB_READ:
        case DULINK_CB_SIZE:
        default:
        {
            duRet = DULINK_CB_ERR_INVALID_ARGUMENT;
            break;
        }
    }

bailout:
    return duRet;
}

/*!
 * @brief Trigger Update to dumaster
 */
static DU_RCODE
triggerUpdate(const char *pPkgPath, bool isAuthPkg)
{
    char     triggerCmd[DU_MAX_CMD_LEN] = {0};
    uint64_t retLen;
    DU_RCODE duRet;
    int32_t len;

    if (isAuthPkg)
    {
        len = snprintf(triggerCmd, sizeof(triggerCmd),
                       "deploy metadata=%s/du_master.json "
                       "auth=%s/auth_conf.json content_root=%s",
                       pPkgPath, pPkgPath, pPkgPath);
    }
    else
    {
        len = snprintf(triggerCmd, sizeof(triggerCmd),
                       "deploy metadata=%s/du_master.json content_root=%s",
                       pPkgPath, pPkgPath);
    }

    if (len >= (int32_t) sizeof(triggerCmd))
    {
        DU_ERR("Command is truncated from %d to %lu\n", len, sizeof(triggerCmd));
        duRet = DULINK_ERR_BUFFER_TOO_SMALL;
    }
    else
    {
        duRet = dulinkWrite(MASTER_CMD, 0,
                    strlen(triggerCmd), triggerCmd, &retLen);
        if (duRet != DU_OK)
        {
            if (duRet == DULINK_CB_ERR_INVALID_ARGUMENT)
            {
                DU_ERR("Invalid argument found when deploying %s, cmd: %s\n",
                       isAuthPkg ? "AuthPkg" : "NormalPkg", triggerCmd);
            }
            else
            {
                DU_ERR("Error Trigger update, err:%#x\n", duRet);
            }
        }
    }
    return duRet;
}

/*!
 * Fetch last server log and print it out
 */
static void printRemoteLog(const char *pEntryPoint, uint64_t bufSize)
{
    char     logBuf[DU_2KB];
    uint64_t retLen;
    DU_RCODE duRet;
    uint32_t startPos;
    uint32_t endPos;

    if (bufSize > DU_2KB)
    {
        bufSize = DU_2KB;
    }

    duRet = dulinkRead(pEntryPoint, 0U, bufSize, logBuf, &retLen);
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to fetch remote log due to error %#x on dulinkRead %s\n",
            duRet, pEntryPoint);
    }
    else if (retLen == 0)
    {
        DU_LOG("Fetched 0 bytes of remote log\n");
    }
    else
    {
        DU_LOG("\n------------- START OF %s LOG -------------\n", pEntryPoint);
        startPos = 0;
        endPos   = 0;
        while (startPos < retLen)
        {
            if ((retLen - startPos) > DU_LOG_MAX_LEN)
            {
                endPos = startPos + DU_LOG_MAX_LEN - 1;
                for (; endPos > startPos; endPos--)
                {
                    if (logBuf[endPos] == '\n')
                    {
                        break;
                    }
                }
                // No LF is found
                if (endPos == startPos)
                {
                    endPos = startPos + DU_LOG_MAX_LEN - 1;
                }
            }
            else
            {
                endPos = retLen - 1;
            }
            DU_LOG("\n%.*s\n", (endPos - startPos + 1), &logBuf[startPos]);
            startPos = endPos + 1;
        }
        DU_LOG("\n------------- END OF %s LOG -------------\n\n", pEntryPoint);
    }
}

/*!
 * @brief Monitor Update
 */
static void *monitorUpdate(void *p)
{
    char        c;
    DUCC_Status status;
    bool        bUpdateError = false;
    bool        bControlledMode = (bool)p;

    for (;;)
    {
        if (DUCC_Get_Current_Status(gpDucc, &status) != DU_OK)
        {
            DU_ERR("Failed to fetch current status!\n");
            goto fail;
        }
        switch (status.state)
        {

            case STATE_DORMANT:
            case STATE_NO_CONNECTIVITY:
            case STATE_UPDATE_AVAILABLE:
                if (bPrintStatus)
                {
                    DU_LOG("Current state: %s\n", DUCC_STATE_TABLE[status.state]);
                }
                break;
            case STATE_UPDATE_IN_PROGRESS:
                if (bPrintStatus)
                {
                    DU_LOG("Current state: %s, progress %d%%\n",
                            DUCC_STATE_TABLE[status.state], status.progress);
                }
                break;
            case STATE_UPDATE_FAILED:
            case STATE_FATAL_ERROR:
                bUpdateError = true;
                DU_LOG("Current state: %s, exit app\n",
                        DUCC_STATE_TABLE[status.state]);
                goto done;
            case STATE_UP_TO_DATE:
                DU_LOG("Current state: %s, exit app\n",
                        DUCC_STATE_TABLE[status.state]);
                goto done;
            default:
                DU_ERR("Invalid state %d\n", status.state);
                goto fail;
        }
        switch (status.pendingAction)
        {
            case PA_NONE:
                break;
            case PA_RUNLEVEL:
                if (bPrintStatus)
                {
                    DU_LOG("Update is waiting for higher run level %d\n",
                            status.pendingActionArgs);
                }
                if (bControlledMode)
                {
                    if (quit)
                    {
                        DU_LOG("Accept RUNLEVEL increase request? (y/n)\n");
                        do
                        {
                            c = getchar();
                        } while (c == ' ' || c == '\n' || c == '\r');

                        if ((c == 'y') || (c == 'Y'))
                        {
                            DU_LOG("Accepted runlevel request, setting RL\n");
                            DUCC_Request_RunLevel(gpDucc, status.pendingActionArgs);
                        }
                        else
                        {
                            DU_LOG("Denied runlevel increase request\n");
                            break;
                        }
                    }
                    else
                    {
                        if (bPrintStatus)
                        {
                            DU_LOG("In Control mode, please set the runlevel in manual\n");
                        }
                    }
                }
                else
                {
                    DUCC_Request_RunLevel(gpDucc, status.pendingActionArgs);
                }
                break;
            case PA_USERACTION:
            default:
                DU_ERR("Invalid pending action %d\n", status.pendingAction);
                goto fail;
        }
        sleep(INTERVAL_PROGRESS);
    }

done:
    if (bUpdateError)
    {
        DU_ERR("[Result]Deploy FAILURE!\n");
    }
    else
    {
        DU_LOG("[Result]Deploy OK!\n");
    }
    if (updateContext(NULL, false) != DU_OK)
    {
        DU_ERR("Failed to clear up context\n");
    }

fail:
    printRemoteLog(MASTER_PERS_LOG, DU_2KB);
    printRemoteLog(TA_TII_CMD_LOG, DU_2KB);

    return NULL;
}

/*!
 * Pre check the content files path's legality
 */
static DU_RCODE checkContentPath(const char *pkgLinkPath)
{
    DU_RCODE    duRet = DU_OK;
    DULINK_ATTR dirAttr;

    duRet = dulinkGetAttribute(pkgLinkPath, &dirAttr);
    if (duRet == DULINK_ERR_NOT_FOUND)
    {
        DU_ERR("The content path is not found: %s\n", pkgLinkPath);
        return DUCOMMON_ERR_GENERIC;
    }
    else if (duRet == DULINK_ERR_ROUTING)
    {
        DU_ERR("Connection for content path %s is not established\n",
                        pkgLinkPath);
        return DUCOMMON_ERR_GENERIC;
    }
    else if (duRet != DU_OK)
    {
        DU_ERR("Could not get attr data for content path: %s, error code %x \n",
                        pkgLinkPath, duRet);
        return DUCOMMON_ERR_GENERIC;
    }
    else
    {
        // Content Path is exist
        if (dirAttr.type == 1U)
        {
            // Found directory. Do nothing.
        }
        else
        {
            DU_ERR("The content path is not a directory: %s\n", pkgLinkPath);
            return DUCOMMON_ERR_GENERIC;
        }
    }

    return DU_OK;
}

/*!
 * @brief Deploy a package
 */
static pthread_t monitorThread;

static DU_RCODE
deployPackage(int argc, const char *argv[])
{
    char        cbPath[DULINK_MAX_PATH] = {0};
    DU_RCODE    duRet = DU_OK;
    uint64_t    retLen;
    DUCC_Status status;
    DULINK_ATTR cbAttr = WRITE_ONLY_ATTRIBUTE;
    int32_t     len;
    const char  *pkgLinkPath = argv[1];
    bool        bPushMode;
    bool        bIsAuthPkg;
    bool        bControlledMode;
    int         c;

    if (argc < 2 || pkgLinkPath == NULL)
    {
        DU_ERR("Package path is NULL!\n");
        return CONTENT_ERR_INVALID_ARGUMENT;
    }

    if (strlen(pkgLinkPath) == 0 || strlen(pkgLinkPath) >= DULINK_MAX_PATH)
    {
        DU_ERR("Pkg path is invalid!\n");
        return CONTENT_ERR_INVALID_ARGUMENT;
    }

    if (checkContentPath(pkgLinkPath) != DU_OK)
    {
        DU_ERR("Pkg path is improper!\n");
        return CONTENT_ERR_NOT_FOUND;
    }

    optind = 0;
    bPushMode = false;
    bIsAuthPkg = false;
    bControlledMode = false;

    while ((c = getopt_long(argc, (char * const *)argv, "tuc", long_options, NULL)) != -1)
    {
        if (c == 't')
            bIsAuthPkg = true;
        else if (c == 'u')
            bPushMode = true;
        else if (c == 'd'){
            bEnableDelta = true;
        }
        else if (c == 'c')
            bControlledMode = true;
    }

    // Check if it's an Auth Package
    if (bIsAuthPkg)
    {
        duRet = checkAuthPackage(pkgLinkPath);
        if (duRet != DU_OK)
        {
            DU_ERR("Failed on checking Auth Package, err:%#x\n", duRet);
            goto fail;
        }
    }

    DU_LOG("Deploy mode is %s\n", bPushMode ? "push" : "pull");
    if (bPushMode && !gbRmCbPath)
    {
        duRet = dulinkExportFile(EXPORT_PUSH_CB, &cbAttr, pushCB,
                (void *)pkgLinkPath);
        if (duRet != DU_OK)
        {
            DU_ERR("Error on export %s\n", EXPORT_PUSH_CB);
            goto fail;
        }

        len = snprintf(cbPath, sizeof(cbPath), "+%s%s/%s",
                gParentPath, CLIENT_NAME, EXPORT_PUSH_CB);
        if (len >= (int32_t) sizeof(cbPath))
        {
            DU_ERR("%s is truncated to %d\n", cbPath, len);
            duRet = DULINK_ERR_BUFFER_TOO_SMALL;
            goto fail;
        }

        duRet = dulinkWrite(TA_PUSH_MODE_NOTIFY, 0, strlen(cbPath), cbPath,
            &retLen);
        if (duRet != DU_OK)
        {
            DU_ERR("Error on register %s to %s\n", cbPath, TA_PUSH_MODE_NOTIFY);
            goto fail;
        }
        gbRmCbPath = true;
    }

    duRet = DUCC_Get_Current_Status(gpDucc, &status);
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to fetch current status!\n");
        goto fail;
    }

    if ((status.state == STATE_DORMANT || status.state == STATE_UPDATE_FAILED ||
         status.state == STATE_UP_TO_DATE) && (status.pendingAction == PA_NONE))
    {
        // Trigger update
        blockSig();
        duRet = triggerUpdate(pkgLinkPath, bIsAuthPkg);
        if (duRet != DU_OK)
        {
            DU_ERR("Failed to trigger Update\n");
            goto fail;
        }
        unBlockSig();
    }
    else
    {
        DU_WARN("Can't deploy, state = %s, pendingAction = %d\n",
                DUCC_STATE_TABLE[status.state], status.pendingAction);
    }

    if (updateContext(pkgLinkPath, bPushMode) != DU_OK)
    {
        DU_ERR("Failed to save update context\n");
    }

    if (pthread_create(&monitorThread, NULL, monitorUpdate, (void *)bControlledMode) != 0)
    {
        DU_ERR("Failed to start monitor update thread\n");
        goto fail;
    }

fail:

    return duRet;
}

/* ----------------------- COMMAND_ABORT -------------------------------------*/
/*!
 * @brief Abort deployment
 */
static DU_RCODE abortDeploy(int argc, const char *argv[])
{
    char        abortCmd[DU_STR_LONG_BUF_SIZE] = DU_MASTER_CMD_ABORT;
    char        cbPath[DULINK_MAX_PATH]        = {0};
    uint64_t    retLen;
    DUCC_Status status;
    DU_RCODE    duRet;
    int32_t     len;

    blockSig();

    if (gbRmCbPath)
    {
        // Remove the push_cb from /tii/push_mode.notify
        len = snprintf(cbPath, sizeof(cbPath), "-%s%s/%s",
                gParentPath, CLIENT_NAME, EXPORT_PUSH_CB);
        if (len >= (int32_t) sizeof(cbPath))
        {
            DU_ERR("%s is truncated to %d\n", cbPath, len);
        }
        else
        {
            duRet = dulinkWrite(TA_PUSH_MODE_NOTIFY, 0, strlen(cbPath), cbPath,
                    &retLen);
            if (duRet != DU_OK)
            {
                DU_ERR("Error on deregister %s to %s\n", cbPath,
                        TA_PUSH_MODE_NOTIFY);
            }
        }
        duRet = dulinkUnlinkFile(EXPORT_PUSH_CB);
        if (duRet != DU_OK)
        {
            DU_WARN("Error %#x on dulinkUnlinkFile %s\n", duRet, EXPORT_PUSH_CB);
        }

        gbRmCbPath = false;
    }

    if (DUCC_Get_Current_Status(gpDucc, &status) != DU_OK)
    {
        DU_ERR("Failed to fetch current status!\n");
    }
    else
    {
        if (status.state == STATE_UPDATE_IN_PROGRESS ||
            status.state == STATE_UPDATE_AVAILABLE)
        {
            (void) dulinkWrite(MASTER_CMD, 0, strlen(abortCmd), abortCmd,
                                &retLen);

            do
            {
                (void) sleep(1U);
                duRet = DUCC_Get_Current_Status(gpDucc, &status);
            } while (duRet == DU_OK && status.state != STATE_UPDATE_FAILED);

            if (duRet != DU_OK)
            {
                DU_ERR("Failed to abort\n");
            }
        }
    }

    (void) updateContext(NULL, false);
    (void) pthread_join(monitorThread, NULL);

    if (DUCC_Request_RunLevel(gpDucc, RL_DORMANT) != DU_OK)
    {
        DU_ERR("Failed to bring runlevel back to dormant\n");
    }
    unBlockSig();
    DU_LOG("Finished abort\n");
    return DU_OK;
}

/* ----------------------- COMMAND_QUERY_BOOTCHAIN ---------------------------*/
/*!
 * @brief Get Bootchain char, BOOTCHAIN_A_VER is A and BOOTCHAIN_B_VER is B
 */
static char getBootChainChar
(
    const char *pVersion,
    uint32_t    versionLen
)
{
    if (strncmp(pVersion, BOOTCHAIN_A_VER, versionLen) == 0)
    {
        return 'A';
    }
    else if (strncmp(pVersion, BOOTCHAIN_B_VER, versionLen) == 0)
    {
        return 'B';
    }
    else if (strncmp(pVersion, BOOTCHAIN_C_VER, versionLen) == 0)
    {
        return 'C';
    }
    else
    {
        return '-';
    }
}

/*!
 * @brief Query bootchain each Tegra is on using DUCC_Get_Versions
 */
static DU_RCODE queryBootchain(int argc, const char *argv[])
{
    DU_RCODE   duRet;
    char      *pBuf = NULL;
    uint32_t   bufSize;
    uint32_t   i;
    bool       unmarkA = true;
    bool       unmarkB = true;

    versionBuffer *pVersionBuf;
    versionEntry  *pVersion;

    duRet = DUCC_Get_Versions(gpDucc, DUCC_IMG_CURRENT, NULL, &bufSize);
    if (duRet != DU_OK)
    {
        DU_ERR("Error fetching version buffer size\n");
        goto fail;
    }

    pBuf = (char *) calloc(bufSize, 1);
    pVersionBuf = (versionBuffer *) pBuf;

    duRet = DUCC_Get_Versions(gpDucc, DUCC_IMG_CURRENT, pVersionBuf, &bufSize);
    if (duRet != DU_OK)
    {
        DU_ERR("Error fetching system version\n");
        goto fail;
    }

    for (i = 0; i<pVersionBuf->numEntries; i++)
    {
        pVersion = &pVersionBuf->pEntry[i];
        if (strncmp(pBuf+pVersion->nameOffset, TA_BOOTCHAIN_VER,
                    pVersion->nameLen) == 0)
        {
            DU_LOG("Tegra A Bootchain Type: %c\n", getBootChainChar(
                pBuf + pVersion->versionOffset, pVersion->versionLen));
            unmarkA = false;
        }
        if (strncmp(pBuf+pVersion->nameOffset, TB_BOOTCHAIN_VER,
                    pVersion->nameLen) == 0)
        {
            DU_LOG("Tegra B Bootchain Type: %c\n", getBootChainChar(
                pBuf + pVersion->versionOffset, pVersion->versionLen));
            unmarkB = false;
        }
    }

    if (unmarkA)
    {
        DU_ERR("Tegra A Bootchain type missing from ducc return\n");
    }
    if (unmarkB)
    {
        DU_LOG("Tegra B is not present or is not connected to TA\n");
    }
fail:
    if (pBuf != NULL)
    {
        free(pBuf);
    }
    return duRet;
}

/*!
 * @brief get partitions version
 */
static DU_RCODE getPartVer(int argc, const char *argv[])
{
    DU_RCODE       ret = DU_OK;
    uint64_t       retLen;
    uint8_t        i;
    uint8_t        ptLevel;
    pvit_t *pPvit;
    char           partName[UNIQUE_NAME_SIZE];
    const char    *nameArray[] = {TA_PVIT, TB_PVIT, TC_PVIT};
    const char    *pvitName;

    for (size_t idx = 0; idx < ARRAY_SIZE(nameArray); idx++)
    {
        pvitName = nameArray[idx];
        ret = dulinkRead(pvitName, 0, sizeof(pvitBuf), pvitBuf, &retLen);
        if (ret != DU_OK)
        {
            DU_ERR("Error %x in dulinkRead %s\n", ret, pvitName);
            goto fail;
        }

        if (retLen != sizeof(pvitBuf))
        {
            DU_ERR("retLen(%lu) is not equal to size of pvit(%lu)\n",
                            retLen, sizeof(pvitBuf));
            ret = CONTENT_ERR_GENERIC;
            goto fail;
        }

        // Skip bch (size fo bch is 8192)
        pPvit = (pvit_t *) (pvitBuf + 8192);

        DU_LOG("Total %u entries in %s, as the following(without chain label):\n",
               pPvit->pvit_table_info.number_of_entries, pvitName);
        for (i = 0U; i < pPvit->pvit_table_info.number_of_entries; i++)
        {
            (void) memset(partName, 0, UNIQUE_NAME_SIZE);
            ptLevel = pPvit->pvit_entry[i].unique_name[0];

            if (ptLevel == 2U)
            {
                (void) memcpy(partName,
                             (char *) (pPvit->pvit_entry[i].unique_name) + 2,
                             UNIQUE_NAME_SIZE - 2U);
            }
            else if (ptLevel == 3U)
            {
                partName[0] = pPvit->pvit_entry[i].unique_name[1] + '0';
                partName[1] = '_';
                (void) memcpy(partName + 2,
                              (char *) (pPvit->pvit_entry[i].unique_name) + 2,
                              UNIQUE_NAME_SIZE - 2U);
            }
            else
            {
                DU_ERR("PT level (%d) is not correct\n", ptLevel);
                ret = CONTENT_ERR_GENERIC;
                goto fail;
            }

            DU_LOG("%u. partition name: %s, version: 0x%x-0x%x-0x%x-0x%x\n", i,
                    partName,
                    pPvit->pvit_entry[i].version[0],
                    pPvit->pvit_entry[i].version[1],
                    pPvit->pvit_entry[i].version[2],
                    pPvit->pvit_entry[i].version[3]);
        }
    }
fail:
    return ret;
}

/* ----------------------- COMMAND_GET_RUNLEVEL ----------------------------*/
/*!
 * @brief Get Runlevel
 */
static DU_RCODE getRunlevel(int argc, const char * argv[])
{
    DU_RCODE duRet = DU_OK;
    DU_RUN_LEVEL curRunlevel = RL_UNRESTRICTED;

    (void)argc;  // argc is not used in this function
    (void)argv;  // argv is not used in this function

    duRet = DUCC_Get_Current_RunLevel(gpDucc, &curRunlevel);
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to get Runlevel, error code %u \n", duRet);
    }
    else if (curRunlevel >= RL_UNRESTRICTED)
    {
        DU_ERR("Get current Runlevel is INVALID,%u!\n", curRunlevel);
        duRet = CONTENT_ERR_GENERIC;
    }
    else
    {
        DU_LOG("Get current Runlevel is %u.\n", curRunlevel);
    }

    return duRet;
}

/* ----------------------- COMMAND_SET_RUNLEVEL ----------------------------*/
/*!
 * @brief set Runlevel
 */
static DU_RCODE setRunlevel(int argc, const char *argv[])
{
    uint32_t    duRunlevel;
    DU_RCODE    duRet;
    int         runLevel;

    if (argc < 2)
        return CONTENT_ERR_INVALID_ARGUMENT;

    runLevel = atoi(argv[1]);

    if (runLevel > 6 || runLevel < 0)
    {
        DU_ERR("Input Runlevel is INVALID!\n");
        return CONTENT_ERR_INVALID_ARGUMENT;
    }
    duRunlevel = (uint32_t)runLevel;
    DU_INFO("The input RunLevel is %u \n", duRunlevel);

    duRet = DUCC_Request_RunLevel(gpDucc, duRunlevel);
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to set Runlevel to %u, error code %u \n",
                        duRunlevel, duRet);
    }
    else
    {
        DU_LOG("The RunLevel is set \n");
    }

    return duRet;
}

/* ----------------------- COMMAND_PRINT_STATUS ----------------------------*/
/*!
 * @brief Enable/Disable deployment status print
 */
static DU_RCODE printStatus(int argc, const char *argv[])
{
    if (argc < 2)
        return CONTENT_ERR_INVALID_ARGUMENT;

    if (strcmp(argv[1], "on") == 0)
    {
        DU_LOG("Enable Print deployment status\n");
        bPrintStatus = true;
    }
    else if (strcmp(argv[1], "off") == 0)
    {
        DU_LOG("Disable Print deployment status\n");
        bPrintStatus = false;
    }
    else
    {
        DU_LOG("Unknown argument %s for print_status\n", argv[1]);
        return CONTENT_ERR_INVALID_ARGUMENT;
    }

    return DU_OK;
}

/* ----------------------- COMMAND_DULINK_READ ----------------------------*/
/*!
 * @brief read file node context and display
 */
static DU_RCODE readNode
(
    const char *pPath
)
{
    DU_RCODE duRet = DU_OK;
    char     readBuf[DU_1KB] = {0};
    int      index = 0;
    uint64_t offset = 0U;
    uint64_t retLen = 0U;

    duRet = dulinkRead(pPath, 0U, sizeof(readBuf), readBuf, &retLen);
    if (duRet != DU_OK)
    {
        DU_ERR("Read %s failed, %#x\n", pPath, duRet);
        goto exit;
    }
    else if (retLen >= sizeof(readBuf))
    {
        DU_WARN("Read buffer full, %lu maybe too small\n", sizeof(readBuf));
        duRet = DULINK_ERR_BUFFER_TOO_SMALL;
    }

    DU_LOG("read length: %lu\n", retLen);
    while (offset < retLen)
    {
        DU_LOG("line[%d]: %s\n", index++, readBuf + offset);
        offset += strlen(readBuf + offset) + 1U;
    }

exit:
    return duRet;
}

/*!
 * @brief dulinkRead node context to local file
 */
static DU_RCODE readNodeToFile
(
    const char *pPath,
    const char *pDst
)
{
    DU_RCODE  duRet = DU_OK;
    uint64_t  offset = 0U;
    uint64_t  len = 0U;
    uint64_t  retLen = 0U;
    ssize_t   writeLen = 0;
    uint64_t  totalSize = 0U;
    int32_t   fd;

    duRet = dulinkGetSize(pPath, &totalSize);
    if (duRet != DU_OK)
    {
        DU_ERR("Error %#x on dulinkGetSize: %s\n", duRet, pPath);
        return duRet;
    }

    fd = open(pDst, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd == -1)
    {
        DU_ERR("open:%s error:%s\n", pDst, strerror(errno));
        return DULINK_ERR_GENERIC;
    }

    while (totalSize > 0U)
    {
        len = totalSize > sizeof(buffer) ? sizeof(buffer):totalSize;

        duRet = dulinkRead(pPath, offset, len, buffer, &retLen);
        if (duRet != DU_OK)
        {
            DU_ERR("Error %#x in dulinkRead: %s\n", duRet, pPath);
            goto exit;
        }

        writeLen = write(fd, buffer, retLen);
        if (writeLen < 0)
        {
            DU_ERR("Fail to write:%s, error:%s\n", pDst, strerror(errno));
            duRet = DULINK_ERR_GENERIC;
            goto exit;
        }

        totalSize -= (uint64_t) writeLen;
        offset += (uint64_t) writeLen;
    }
    (void) fsync(fd);
exit:
    (void) close(fd);
    return duRet;
}

/*!
 * @brief dulinkRead node
 */
static DU_RCODE duRead(int argc, const char *argv[])
{
    DU_RCODE    duRet = DU_OK;

    if (argc == 2)
    {
        duRet = readNode(argv[1]);
        if (duRet != DU_OK)
        {
            DU_ERR("Error %#x in readNode\n", duRet);
            return duRet;
        }
    }
    else if (argc == 3)
    {
        duRet = readNodeToFile(argv[1], argv[2]);
        if (duRet != DU_OK)
        {
            DU_ERR("Error %#x in readNodeToFile\n", duRet);
            return duRet;
        }
    }
    else
    {
        DU_ERR("Wrong arguments number: %d\n", argc);
        return CONTENT_ERR_INVALID_ARGUMENT;
    }

    return DU_OK;
}

/* ----------------------- COMMAND_DULINK_WRITE ----------------------------*/
/*!
 * @brief dulinkWrite node
 */
static DU_RCODE duWrite(int argc, const char *argv[])
{
    DU_RCODE    duRet = DU_OK;
    uint64_t    retLen = 0U;
    uint64_t    dataLen = 0U;

    if (argc < 3)
    {
        return CONTENT_ERR_INVALID_ARGUMENT;
    }

    dataLen = strlen(argv[1]);
    if (dataLen == 0U)
    {
        DU_ERR("Write data is empty\n");
        return CONTENT_ERR_INVALID_ARGUMENT;
    }

    duRet = dulinkWrite(argv[2], 0U, dataLen, (char *)argv[1], &retLen);
    if (duRet != DU_OK)
    {
        DU_ERR("Error %#x on dulinkWrite %s\n", duRet, argv[2]);
        return duRet;
    }

    return DU_OK;
}

/* ----------------------- COMMAND_DULINK_LIST ----------------------------*/
/*!
 * @brief list the nodes in a directory
 */
static DU_RCODE duList(int argc, const char *argv[])
{
    DU_RCODE    duRet = DU_OK;
    uint64_t    retLen = 0U;
    uint64_t    offset = 0U;
    uint32_t    childNum = 0U;
    char        readBuf[DU_1KB] = {0};
    char        childPath[DULINK_MAX_PATH] = {0};

    if (argc < 2)
    {
        return CONTENT_ERR_INVALID_ARGUMENT;
    }

    duRet = dulinkRead(argv[1], 0, sizeof(readBuf), readBuf, &retLen);
    if (duRet != DU_OK)
    {
        DU_ERR("Read %s failed, %#x\n", argv[1], duRet);
        return DULINK_ERR_GENERIC;
    }
    else if (retLen == sizeof(readBuf))
    {
        DU_ERR("Read buffer full, %lu maybe too small\n", sizeof(readBuf));
        duRet = DULINK_ERR_BUFFER_TOO_SMALL;
        return DULINK_ERR_GENERIC;
    }

    while (offset < retLen)
    {
        // get full path of the file node
        (void) snprintf(childPath, sizeof(childPath), "%s/%s", argv[1],
                        readBuf + offset);

        DU_LOG("SubNode %d: %s\n", childNum, childPath);
        offset += strlen(readBuf + offset) + 1U;
        childNum++;
    }

    return DU_OK;
}

/* ----------------------- COMMAND_MAX ---------------------------------------*/
/*!
 * @brief Read Context
 */

static DU_RCODE
getContext(char *pPkgPath, bool *pbExportNeeded, bool *pbPush)
{
    char            context[DU_1KB] = {0};
    static char     elmtArray[JSON_ELEMENT_SIZE];
    json_t          *pJson;
    jsonElement_t   *pJsonRootElmt;
    int32_t         jsonLen = 0;
    uint64_t        retLen;
    uint64_t        jsonTmpVal = 0U;
    DU_RCODE        duRet = DU_OK;

    if (dulinkRead(content_provider.persistCtxPath, 0,
                    sizeof(context), context, &retLen) != DU_OK)
    {
        // we just ignore the fail
        DU_INFO("Failed to read persistent context, assuming empty\n");
    }
    if (strlen(context) == 0)
    {
        // emtpy context
        return CONTENT_ERR_GENERIC;
    }

    // Parser JSON file
    pJson = jsonParser(context, sizeof(context),
                        elmtArray, JSON_ELEMENT_SIZE);
    if (pJson == NULL)
    {
        DU_ERR("Error on jsonParser\n");
        duRet = CONTENT_ERR_NOT_FOUND;
        goto out;
    }

    // Get root element
    pJsonRootElmt = jsonGetRoot(pJson);
    if (pJsonRootElmt == NULL)
    {
        DU_ERR("Error on jsonGetRoot\n");
        duRet = CONTENT_ERR_NOT_FOUND;
        goto out;
    }

    // get pkgPath
    jsonLen = jsonGetStrByName(pJsonRootElmt, "pkgPath", pPkgPath, DU_PATH_MAX);
    if (jsonLen == 0)
    {
        DU_ERR("Error on load %s\n", "pkgPath");
        duRet = CONTENT_ERR_NOT_FOUND;
        goto out;
    }

    // get bExportNeeded.
    jsonLen = jsonGetUint64ByName(pJsonRootElmt, "bExportNeeded", &jsonTmpVal);
    if (jsonLen < 0)
    {
        jsonTmpVal = 0;
    }
    // Convert from uint64 to bool
    *pbExportNeeded = (jsonTmpVal == 0U)? false : true;

    // get bPush.
    jsonLen = jsonGetUint64ByName(pJsonRootElmt, "bPush", &jsonTmpVal);
    if (jsonLen < 0)
    {
        DU_ERR("Error on load %s\n", "bPush");
        duRet = CONTENT_ERR_NOT_FOUND;
        goto out;
    }
    // Convert from uint64 to bool
    *pbPush = (jsonTmpVal == 0U)? false : true;

out:
    return duRet;
}

/*!
 * @brief signal handler
 */
static void sigHandler(int signum) {
    DU_LOG("signal %d received \n", signum);

    if (signum == SIGINT || signum == SIGTERM)
    {
        DU_LOG("Exiting....\n");
        cleanUpConn();
        exit(EXIT_SUCCESS);
    }
    else
    {
        DU_LOG("Forcing abort update...\n");
        abortDeploy(0, NULL);
        exit(EXIT_SUCCESS);
    }
}

static DU_RCODE leave(int argc, const char *argv[])
{
    pthread_cancel(monitorThread);
    quit = true;

    return DU_OK;
}

#define MAX_CMDLINE         255

static DU_RCODE sys(int argc, const char *argv[])
{
    char cmd[MAX_CMDLINE];
    size_t offset = 0;

    for (int i = 1; i < argc; i++)
    {
        size_t len = strlen(argv[i]);

        memcpy(&cmd[offset], argv[i], len);
        offset += len;
        cmd[offset++] = 0x20;
    }

    cmd[offset] = 0;
    if(system(cmd) != 0)
    {
        // Ignore this error
    }

    return DU_OK;
}

/*!
 * @brief get cmdline from interaction
 */
#define TABLE_LEN           25
#define PROMPT              "(client)"
#define FULL_CANDIDATE      ((1 << ARRAY_SIZE(gCmds)) - 1)

static char cmdLine[MAX_CMDLINE];

static void getLine(int *argc, const char *argv[])
{
    uint64_t index = 0;
    int ch;
    size_t i;
    bool accept = false;
    uint64_t candidate = FULL_CANDIDATE;
    char *line = cmdLine;
    int32_t CalculateResult;

    printf(PROMPT);

    while (accept == false)
    {
        ch = getchar();

        switch (ch)
        {
        case 0x1b:
            // arrow keys
            // ignore '['
            if (getchar() != EOF)
            {
                ch = getchar();
            }
            switch (ch)
            {
            case 0x41:
                break;
            case 0x42:
                break;
            default:
                break;
            }
            break;
        case 0x7f:
            // backspace
            candidate = FULL_CANDIDATE;
            if (index > 0)
            {
                printf("\r");
                for (i = 0; i < (index + strlen(PROMPT)); i++)
                {
                    putchar(0x20);
                }
                line[--index] = 0;
            }
            printf("\r"PROMPT"%s", line);
            break;
        case 0x20:
            if (index != 0 && index < MAX_CMDLINE)
            {
                line[index++] = ch;
            }
            putchar(ch);
            break;
        case '\n':
        {
            const char **p;

            putchar(ch);
            line[index] = 0;
            accept = true;

            for (*argc = 0, argv[0] = NULL, p = &argv[0], i = 0; i < index; i++)
            {
                if (line[i] == ' ')
                {
                    if (*p != NULL)
                    {
                        line[i] = 0;
                        argv[*argc] = NULL;
                        p = &argv[*argc];
                    }

                    continue;
                }

                if (*p == NULL)
                {
                    (*argc)++;
                    *p = &line[i];
                }
            }

            break;
        }
        case '\t':
            for (i = 0; candidate != 0 && i < ARRAY_SIZE(gCmds); i++)
            {
                if ((candidate & (1 << i)) != 0)
                {
                    if (0 != strncmp(gCmds[i].cmd, line, index))
                    {
                        candidate &= ~(1 << i);
                    }
                }
            }

            if (candidate != 0)
            {
                if ((candidate & (candidate - 1)) == 0)
                {
                    CalculateResult = 63 - __builtin_clzl(candidate);
                    if (CalculateResult >= 0)
                    {
                        (void) strlcpy(&line[index], &gCmds[CalculateResult].cmd[index], MAX_CMDLINE - index);
                    }
                    else
                    {
                        DU_ERR("cmds array address negative offset\n");
                    }
                    line[strlen(line)] = '\0';
                    printf("\r"PROMPT"%s ", line);
                    index = strlen(line);
                    line[index++] = 0x20;
                }
                else
                {
                    int tmp = 0;

                    printf("\n");

                    for (i = 0; i < ARRAY_SIZE(gCmds); i++)
                    {
                        if ((candidate & (1 << i)) != 0)
                        {
                            printf("%s", gCmds[i].cmd);
                            for (int j = strlen(gCmds[i].cmd); j < TABLE_LEN; j++)
                                putchar(0x20);

                            if ((tmp++ % 4) == 3)
                                printf("\n");
                        }
                    }

                    line[index] = 0;
                    printf("\n"PROMPT"%s", line);
                }
            }
            break;
        default:
            if (index < MAX_CMDLINE)
            {
                putchar(ch);
                line[index++] = ch;
            }
            break;
        }
    }
}

static void termConfig(bool set)
{
    static struct termios old, current;

    if (!set)
    {
        tcsetattr(0, TCSANOW, &old);
    }
    else
    {
        tcgetattr(0, &old);
        current = old;
        current.c_lflag &= ~ICANON; /* disable buffered i/o */
        current.c_lflag &= ~ECHO; /* set no echo mode */
        tcsetattr(0, TCSANOW, &current);
    }
}

/*!
 * @brief main function
 */

int main(int ac, char* av[])
{
    int         optionIndex = -1;
    int         c;
    DU_RCODE    duRet = DU_OK;
    char        linkPath[DU_PATH_MAX]  = {0};
    bool        bPushMode              = false;
    bool        bLocalPkg              = false;
    size_t      i;
    int         argc;
    const char  *argv[32];

    // Set up signal handler
    struct sigaction sigact =
    {
        .sa_handler = sigHandler,
        .sa_flags = 0
    };
    sigemptyset(&sigact.sa_mask);
    sigaction(SIGINT, &sigact, NULL);
    sigaction(SIGTERM, &sigact, NULL);
    sigaction(SIGQUIT, &sigact, NULL);

    duRet = initDulinkConn();
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to initialize dulink connection\n");
        goto fail;
    }
    duRet = initDuccConn();
    if (duRet != DU_OK)
    {
        DU_ERR("Failed to initialize ducc connection\n");
        goto fail;
    }

    // set up terminal for interaction command line
    termConfig(true);

    argc = 0;
    quit = false;
    // Process command line options
    c = getopt_long(ac, av, "hp:gaqs:d:r:w:l:",
                    long_options, &optionIndex);

    if (c != -1 && c != '?')
    {
        quit = true;
        argc = ac - 1;

        if (optionIndex == -1)
        {
            for (i = 0; i < ARRAY_SIZE(long_options); i++)
            {
                if (long_options[i].val == c)
                {
                    optionIndex = i;
                    break;
                }
            }
        }

        argv[0] = long_options[optionIndex].name;

        for (i = 1; (int)i < argc; i++)
        {
            argv[i] = av[i + 1];
        }
    }

    if (getContext(linkPath, &bLocalPkg, &bPushMode) == DU_OK)
    {
        bool bControlledMode;

        DU_LOG("Previous context found with following parameters:\n");
        DU_LOG("-p %s %s\n",
            linkPath,
            bPushMode ? "--push" : "");

        if (!quit)
        {
            optind = 1;
            bControlledMode = getopt(argc, (char * const *)argv, "c") == 'c';
            if (pthread_create(&monitorThread, NULL, monitorUpdate, (void *)bControlledMode) != 0)
            {
                DU_ERR("Failed to start monitor update thread\n");
                goto fail;
            }
        }
    }

    while (1)
    {
        if (argc != 0)
        {
            for (i = 0; i < ARRAY_SIZE(gCmds); i++)
            {
                if (strcmp(gCmds[i].cmd, argv[0]) == 0)
                {
                    break;
                }
            }

            if (i < ARRAY_SIZE(gCmds) && gCmds[i].func != NULL)
            {
                gCmds[i].func(argc, argv);
            }
            else if (i >= ARRAY_SIZE(gCmds))
            {
                DU_LOG("Command not found\n");
            }
        }

        if (quit)
            break;

        getLine(&argc, argv);
    }

    termConfig(false);

fail:
    pthread_join(monitorThread, NULL);
    cleanUpConn();
    return (int) duRet;
}
