/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * the CANN Open Software License Agreement Version 2.0.
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*
 * Compatibility include for the SIMT Jetty API.
 *
 * The implementation lives in hcomm_jetty_simt.h. Keep this historical path
 * as a thin wrapper so existing SIMT kernels do not carry a second copy of the
 * Jetty implementation and so it cannot collide with jetty/hcomm_jetty.h.
 */
#ifndef INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_COMPAT_H
#define INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_COMPAT_H

#if defined(USE_SIMT)
#include "hcomm_jetty_simt.h"
#endif

#endif // INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_COMPAT_H
