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
 * @brief Dumps and prints the channel diagnostics at a host safe point after stream synchronization fails.
 * @param [in] channel Channel handle owned by the operator.
 * @return 0 on success, -1 on failure.
 * @note Safe to call unconditionally. If no exception has been recorded, this API returns success without printing.
 */
int32_t HcommChannelInfoDump(ChannelHandle channel);

} // namespace AscendC

#endif // ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_H
