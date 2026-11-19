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
 * \file asccomm_jetty_simt_impl_def.h
 * \brief Declarations the public SIMT Jetty header needs before class asccomm_jetty.
 */

#ifndef IMPL_COMM_API_AICORE_JETTY_IMPL_ASCCOMM_JETTY_SIMT_IMPL_DEF_H
#define IMPL_COMM_API_AICORE_JETTY_IMPL_ASCCOMM_JETTY_SIMT_IMPL_DEF_H

#include "../../asccomm/common/asccomm_simt_utils.h"

// SIMT Jetty is only available on v310. The ASC driver compiles each translation unit twice:
// __NPU_ARCH__ is 3510, 9201, or 9202 in the device pass and undefined in the host pass. The public asccomm_jetty
// holds a jetty_impl member, so the class must stay visible in both passes; gating on
// "not some other arch" keeps v310 and the host pass while still excluding other platforms.
#if !defined(__NPU_ARCH__) || __NPU_ARCH__ == 3510 || __NPU_ARCH__ == 9201 || __NPU_ARCH__ == 9202
#include "platform_v310/asccomm_simt_urma_jetty_def.h"
#endif

#endif // IMPL_COMM_API_AICORE_JETTY_IMPL_ASCCOMM_JETTY_SIMT_IMPL_DEF_H
