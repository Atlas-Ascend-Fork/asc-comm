/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file hcomm_impl_def.h
 * \brief Hcomm implementation definition
 */

#if !defined(__ASCENDC_INCLUDE_INTERNAL_HEADERS__)
#pragma message( \
    "This is an internal Hcomm header. Include \"hcomm/hcomm.h\" instead.")
#define __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#define __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_IMPL_DEF_H__
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_IMPL_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_IMPL_DEF_H

#include "hcomm_base.h"

#if defined(__NPU_ARCH__) && __NPU_ARCH__ == 2201
#include "../roce/platform/v220/hcomm_roce_def.h"
#endif

#if defined(__NPU_ARCH__) && __NPU_ARCH__ == 3510
#include "../roce/platform/v310/hcomm_roce_def.h"
#include "../ubc/platform/v310/hcomm_urma_def.h"
#endif

#endif
#if defined(__UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_IMPL_DEF_H__)
#undef __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#undef __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_IMPL_DEF_H__
#endif
