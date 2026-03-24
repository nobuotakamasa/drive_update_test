/*
 * Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/*!
 * @file    dulog.h
 * @brief   DU logging interface
 */

#ifndef DULOG_H_
#define DULOG_H_

#ifdef USE_INT_LOGGING
#include "dulog_int.h"
#else
#include <stdio.h>
#define DU_LOG(fmt, ...) (void) fprintf(stderr, fmt, ##__VA_ARGS__);

#define DU_DBG(fmt, ...)
#define DU_INFO(fmt, ...) DU_LOG("[INFO]" fmt, ##__VA_ARGS__)
#define DU_ATTN(fmt, ...) DU_LOG("[ATTN]" fmt, ##__VA_ARGS__)
#define DU_WARN(fmt, ...) DU_LOG("[WARN]" fmt, ##__VA_ARGS__)
#define DU_ERR(fmt, ...)  DU_LOG("[ERR]"  fmt, ##__VA_ARGS__)
#define DU_CRIT(fmt, ...) DU_LOG("[CRIT]" fmt, ##__VA_ARGS__)
#endif

#endif // DULOG_H_
