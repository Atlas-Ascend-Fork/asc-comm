/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_H
#define ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_H

#include "hcomm/hcomm_common.h"
#include "hcomm_channel_dump_info.h"

#include <cstdint>

namespace AscendC {

/*!
 * @brief Registers the exception callback for the target kernel.
 * @param [in] kernelSymbol Kernel symbol used to bind the exception callback.
 * @return 0 on success, -1 on failure.
 * @note Call this before launching the target kernel. Once the kernel triggers an AIC Error, the framework only
 *       records the exception state and does not perform D2H, printing, or launch operations inside the callback.
 */
int32_t HcommChannelExceptionCallbackRegister(const void *kernelSymbol);

/*!
 * @brief Dumps, parses, and prints the channel diagnostics at a host safe point after stream synchronization fails.
 * @param [in] channel Channel handle owned by the operator.
 * @param [out] dump Parsed dump data. Set to nullptr when the caller only needs the default printing side effect.
 * @return 0 on success, -1 on failure.
 * @note Safe to call unconditionally. After the exception callback has been registered, this API returns success
 *       without printing when no exception is pending. Without callback binding, the API performs a direct dump.
 * @note When @p dump is not nullptr, the API fills the parsed dump structure before printing.
 */
int32_t HcommChannelInfoDump(ChannelHandle channel, HcommDfxChannelDumpData *dump = nullptr);

} // namespace AscendC

#if !defined(__aicore__) || defined(HCOMM_DFX_INCLUDE_IMPLEMENTATION)
#include "hcomm/dfx/host/hcomm_dfx.h"
#endif

#endif // ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_H
