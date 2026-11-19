/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef INCLUDE_LEGACY_HCOMM_HCOMM_COMMON_H
#define INCLUDE_LEGACY_HCOMM_HCOMM_COMMON_H

// Forwards to the shared communication types (namespace asc). The legacy public headers
// include this shell before the implementation chain, so the types are available to them under
// both include-path configurations.
//
// Deprecation attribute for the legacy Hcomm interfaces. Definition mirrors the devkit
// c_api/defs/macro.h so both headers can coexist in one translation unit.
#ifndef ASC_DEPRECATED
#define ASC_DEPRECATED(version, removal_date, replacement)                                                  \
    [[deprecated("Deprecated since " #version ", Will be removed after " removal_date ", Use " #replacement \
                 " instead.")]]
#endif
#if __has_include("../asccomm/asccomm_common.h")
#include "../asccomm/asccomm_common.h"
#else
#include "../../comm_api/aicore/asccomm/asccomm_common.h"
#endif

#endif // INCLUDE_LEGACY_HCOMM_HCOMM_COMMON_H
