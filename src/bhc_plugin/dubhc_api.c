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
 * @file  dubhc_api.c
 * @brief Implementation of boot health check API
 */

/* ------------------------ Drive Update Includes --------------------------- */
#include "dubhc_api.h"

/*
 * Trace: dubhc
 */

/*
 * Trace: dubhc-duBootHealthCheckUserCallback
 */
bool duBootHealthCheckUserCallback
(
    void
)
{
    // Fill your tests here, default returns success
#ifndef DU_BHC_FAILURE
    return true;
#else
    return false;
#endif
}
