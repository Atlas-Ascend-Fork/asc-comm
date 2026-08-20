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
 * \file asccomm_simt_impl_def.h
 * \brief asccomm SIMT implementation definition
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal asccomm header. Please include public asccomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_IMPL_DEF_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_SIMT_IMPL_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_SIMT_IMPL_DEF_H

#include "../common/hcomm_simt_base.h"

// SIMT is only available on v310. Other platforms fall back to the asccomm_impl primary template,
// whose members all return asccomm_failed.
#if defined(__NPU_ARCH__) && __NPU_ARCH__ == 3510
#include "platform_v310/hcomm_simt_urma_def.h"
#endif

#endif // IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_SIMT_IMPL_DEF_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_IMPL_DEF_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_IMPL_DEF_H
#endif
