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

#ifndef DUJSONPARSER_H_
#define DUJSONPARSER_H_

#include <stddef.h>
#include <stdbool.h>
#include "utils.h"

/// json element's type
enum JSONTYPE
{
    JSON_UNKNOWN,
    JSON_OBJECT,
    JSON_ARRAY,
    JSON_STR,
    JSON_NUM
};

typedef struct json json_t;
typedef struct jsonElement jsonElement_t;

/*!
 * @brief Get string value from element by name
 *
 * @param[in] object(jsonElement_t*)
 *      json element
 * @param[in] name(char*)
 *      json string element's name
 * @param[out] des(char*)
 *      user buffer to copy string to
 * @param[in] size(size_t)
 *      user buffer max size
 *
 * @return size_t
 *      length of bytes to be copied
 */
size_t jsonGetStrByName
(
    jsonElement_t *object,
    const char    *name,
    char          *des,
    size_t        size
);

/*!
 * @brief Get bool value by copying string element's complete value to buffer
 *
 * @param[in] object(jsonElement_t*)
 *      json element
 * @param[in] name(char*)
 *      json string element's name
 * @param[out] des(char*)
 *      user buffer to copy string to
 * @param[in] size(size_t)
 *      user defined data size
 *
 * @return true/false
 *      bool value of result, return true only when succeed
 *      in copying string to buffer with expected data size,
 *      else return false when fail to copy string or copied
 *      string return size with 0
 */
bool jsonBoolGetStrByName
(
    jsonElement_t *object,
    const char    *name,
    char          *des,
    size_t        size
);

/*!
 * @brief Get string value from element by index
 *
 * @param[in] element(jsonElement_t*)
 *      json element
 * @param[in] index(uint32_t)
 *      index number value
 * @param[out] des(char*)
 *      user buffer to copy string to
 * @param[in] size(size_t)
 *      user buffer max size
 *
 * @return size_t
 *      length of bytes to be copied
 */
size_t jsonGetStrByIndex
(
    jsonElement_t *element,
    uint32_t      index,
    char          *des,
    size_t        size
);

/*!
 * @brief Get uint64_t value from element by name
 *
 * @param[in] object(jsonElement_t*)
 *      json element
 * @param[in] name(char*)
 *      json string element's name
 * @param[in] val(uint64_t*)
 *      val to store uint64_t
 *
 * @return 0 on success, < 0 if any error
 */
int32_t jsonGetUint64ByName
(
    jsonElement_t *object,
    const char    *name,
    uint64_t      *val
);

/*!
 * @brief Get uint64_t value from element by index
 *
 * @param[in] element(jsonElement_t*)
 *      json element
 * @param[in] index(uint32_t)
 *      array/object index, start from 0
 * @param[in] val(uint64_t*)
 *      val to store uint64_t
 *
 * @return 0 on success, < 0 if any error
 */
int32_t jsonGetUint64ByIndex
(
    jsonElement_t *element,
    uint32_t      index,
    uint64_t      *val
);

/*!
 * @brief Get uint64_t value from number element
 *
 * @param[in] element(jsonElement_t*)
 *      element to get number
 * @param[in] val(uint64_t*)
 *      val to store uint64_t
 *
 * @return 0 on success, < 0 if any error
 */
int32_t jsonGetUint64(const jsonElement_t *numElement, uint64_t *val);

/* Json lib standard interfaces */

#ifndef PHYICD
/*!
 * @brief Get element content from file
 *
 * @param[in] element(jsonElement_t*)
 *      element to get content
 * @param[in] desBuf(char*)
 *      buf to copy content to
 * @param[in] size(size_t)
 *      size of buf
 *
 * @return size_t
 *      >0: bytes to be copied, if it > size, there's truncation
 *      <0: error code
 */
size_t jsonPrintElement(const jsonElement_t *element, char *desBuf, size_t size);
#endif /* PHYICD */

/*!
 * @brief Get root element of json file
 *
 * @param[in] json(json_t*)
 *      json file pointer
 *
 * @return jsonElement_t *
 *      root element
 */
jsonElement_t *jsonGetRoot(const json_t *jsonData);

/*!
 * @brief Get element type
 *
 * @param[in] element(jsonElement_t*)
 *      element to get type of
 *
 * @return enum JSONTYPE
 *      type of the specified element
 */
enum JSONTYPE jsonGetType(const jsonElement_t *element);

/*!
 * @brief Get object's name
 *
 * @param[in] element(jsonElement_t*)
 *      json element
 * @param[in] des(char*)
 *      user buffer to copy string to
 * @param[in] size(size_t)
 *      user buffer max size
 *
 * @return name size to be copied
 */
size_t jsonGetName(const struct jsonElement *element, char *des,
                   size_t size);

/*!
 * @brief Get element from array by index
 *
 * @param[in] element(jsonElement_t*)
 *      json element
 * @param[in] index(uint32_t)
 *      array/object index, start from 0
 *
 * @return jsonElement_t *
 *      element found
 * @return NULL
 *      Not a correct request
 */
jsonElement_t *jsonGetByIndex(const jsonElement_t *element, uint32_t index);

/*!
 * @brief Get element from object by member name
 *
 * @param[in] object(jsonElement_t*)
 *      json object
 * @param[in] name(char*)
 *      member name to match
 *
 * @return jsonElement_t *
 *      element found
 * @return NULL
 *      Not found or not a correct request
 */
jsonElement_t *jsonGetByName(const jsonElement_t *object, const char *name);

/*!
 * @brief Get array size of json array
 *
 * @param[in] array(jsonElement_t*)
 *      json array
 *
 * @return size of array
 */
uint32_t jsonGetArraySize(const jsonElement_t *array);

/*!
 * @brief Get integer value from element
 *
 * @param[in] numElement(jsonElement_t*)
 *      json nummeric element
 * @param[in] val(uint64_t*)
 *      to store the converted double value
 *
 * @return 0 on success, < 0 if any error
 */
int32_t jsonGetNum(const jsonElement_t *numElement, DUfloat64_t *val);

/*!
 * @brief Get string value from element
 *
 * @param[in] strElement(jsonElement_t*)
 *      json string element
 * @param[in] des(char*)
 *      user buffer to copy string to
 * @param[in] size(size_t)
 *      user buffer max size
 *
 * @return size_t
 *      length of bytes to be copied
 */
size_t jsonGetStr(const jsonElement_t *strElement, char *des, size_t size);

/*!
 * @brief json parser function
 *
 * @param[in] pFileBuf(char*)
 *      buffer contains json file
 * @param[in] fileSize(size_t)
 *      json file size
 * @param[in] buffer(char*)
 *      user provided buffer for json parse to store the parse result
 * @param[in] size(size_t)
 *      user buffer size
 *
 * @return json_t*
 *      new json_t pointer to access the parsed json
 */
json_t *jsonParser(const char *pFileBuf, size_t fileSize, char *jsonBuffer, size_t size);

#ifdef JSON_TEST
void jsonOutput(json_t *json);
#endif

#endif