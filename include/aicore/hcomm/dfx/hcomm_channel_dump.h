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
 * @brief Dumps, parses, and prints channel diagnostics.
 * @param [in] channel Channel handle owned by the operator.
 * @param [out] buf Optional caller buffer that receives the printable dump string. Pass nullptr when only the
 *              stdout output is needed. When non-null, provide a buffer large enough for the readable dump text.
 * @param [in] len Size of @p buf in bytes. Ignored when @p buf is nullptr.
 * @return 0 when the dump completes successfully, or when a print-only dump is queued; -1 on failure.
 * @note Pass nullptr and zero for @p len to request a print-only dump. This form captures the current ACL context,
 *       starts an internal host worker, and returns after the request is queued; the worker performs the queue-reader
 *       dump after a short delay. A process-exit fallback attempts any queued-but-unconsumed print-only dump once.
 *       No caller buffer is available in this mode, so output is written by the DFX logger.
 * @note When @p buf is non-null, the API is synchronous: the buffer is valid to read after the call returns. After
 *       the exception callback has been registered, a synchronous call without a pending exception returns success
 *       without printing. Without callback binding, it performs a direct dump.
 * @note If @p len is too small, the synchronous form truncates the string, null-terminates it, and returns -1.
 */
int32_t HcommChannelInfoDump(uint64_t channel, char *buf = nullptr, uint32_t len = 0);

} // namespace AscendC

#endif // ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_H
