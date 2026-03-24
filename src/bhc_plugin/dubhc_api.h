/*
 * Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/*!
 * @file  dubhc_api.h
 * @brief User API of Dubhc plugin
 */

#ifndef DUBHC_API_H_
#define DUBHC_API_H_

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------ System Includes --------------------------------- */
#include <stdbool.h>

/* ------------------------ Public Functions -------------------------------- */
/*!
 * @brief defined Boot Health Check callback function.
 *
 * @language C
 *
 * @interface DU-BHC
 *
 * @return true
 *          Success on boot health check.
 *
 * @return false
 *          Failure on boot health check.
 *
 */
bool duBootHealthCheckUserCallback
(
    void
);

#ifdef __cplusplus
}
#endif
#endif
