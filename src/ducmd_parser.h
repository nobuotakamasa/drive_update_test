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
 * @file  ducmd_parser.h
 * @brief Handles parsing commands into tokens for DU COMMON or installers
 */

#ifndef DUCMD_PARSER_H_
#define DUCMD_PARSER_H_

#include "ducommon.h"
#include "utils.h"

typedef enum   CMD_TOK_TYPE CMD_TOK_TYPE;
typedef struct CMD_TOKEN    CMD_TOKEN, *PCMD_TOKEN;

/// command token type
enum CMD_TOK_TYPE
{
    CMD_TOK_TYPE_STRING = 0,
    CMD_TOK_TYPE_OPERATOR
};

//
// Each notable section of an input string will be parsed the CMD_TOKEN data
// struture to make it easy to work with the data
//
struct CMD_TOKEN
{
    // Position of token start in input string
    uint32_t     start;
    // Position of end of token in input string
    uint32_t     end;
    // Length of token string in bytes
    uint32_t     size;
    // Token type
    CMD_TOK_TYPE type;
};

/*!
 * @brief Parse command line input string into tokens for easier input handling by
 * command callback functions. This function may modify pInputStr by deleting
 * quotes, trailing/leading spaces, and escape characters
 *
 * @param[in, out] pInputStr(char*)
 *      Input string containing the command line input to parse
 * @param[out] pNumTokens(uint32_t*)
 *      Number of tokens parsed out of the input
 * @param[out] pTokArray(PCMD_TOKEN)
 *      Pointer to array of CMD_TOKEN to be used to store token info
 * @param[in] tokArraySize(uint32_t)
 *      Size of pTokArray
 *
 * @return DU_OK
 *      Successfully parsed input into tokens
 * @return DUCOMMON_ERR_*
 *      Invalid syntax or other error has occurred
 */
DU_RCODE parseStringIntoTokens
(
    char         *pInputStr,
    uint32_t     *pNumTokens,
    PCMD_TOKEN    pTokArray,
    uint32_t      tokArraySize
);

/*!
 * @brief Validate order of tokens to so they may be processed in command handler
 *
 * @param[in] pTokArray(const_CMD_TOKEN*)
 *      Pointer to token array to vaidate
 * @param[in] numTokens(uint32_t)
 *      Size of token array
 *
 * @return DU_OK
 *      Token ordering syntax is valid
 * @return DUCOMMON_ERR_SYNTAX
 *      Invalid ordering of tokens
 */
DU_RCODE validateTokenArray
(
    const CMD_TOKEN    *pTokArray,
    uint32_t            numTokens
);

/*!
 * @brief Strip a value from a string which is preceeded by pNeedle. String will be
 * copied only up to next space or NULL character
 *
 * @param[in] pInputStr(const_char*)
 *      Input string to search through
 * @param[in] pTokArray(const_CMD_TOKEN*)
 *      Tokenized array for pInputStr
 * @param[in] tokArrayLen(uint64_t)
 *      Number of tokens in pTokArray
 * @param[in] pNeedle(const_char*)
 *      Delimiter tag to search for
 * @param[out] pOutputStr(char*)
 *      Value parsed from input delimiter tag
 * @param[in] outputLen(uint64_t)
 *      Size of output buffer
 *
 * @return DU_OK
 *      Tag stripped successfully
 * @return DUCOMMON_ERR_NOT_FOUND
 *      Delimeter tag not found in input
 * @return DUCOMMON_ERR_BUFFER_TOO_SMALL
 *      Value too large to save to pOutputStr
 */
DU_RCODE stripTagFromInput
(
    const char       *pInputStr,
    const CMD_TOKEN  *pTokArray,
    uint64_t          tokArrayLen,
    const char       *pNeedle,
    char             *pOutputStr,
    uint64_t          outputLen
);

#endif // DUCMD_PARSER_H_
