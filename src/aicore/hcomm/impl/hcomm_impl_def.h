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

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_IMPL_DEF_H
#endif

#ifndef IMPL_COMM_API_AICORE_HCOMM_IMPL_HCOMM_IMPL_DEF_H
#define IMPL_COMM_API_AICORE_HCOMM_IMPL_HCOMM_IMPL_DEF_H

#include "../common/hcomm_base.h"

#if defined(__NPU_ARCH__) && __NPU_ARCH__ == 2201
#include "hcomm_v220_impl.h"
#endif

#if defined(__NPU_ARCH__) && __NPU_ARCH__ == 3510
#include "hcomm_v310_impl.h"
#endif

#endif
#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_IMPL_DEF_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_IMPL_DEF_H
#endif
