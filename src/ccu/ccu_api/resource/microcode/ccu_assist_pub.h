/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_CONTEXT_ASSIST_PUB_H
#define CCU_CONTEXT_ASSIST_PUB_H

#include <cstdint>

namespace asc {
namespace ccu_rep {

// 辅助函数
uint64_t get_token_info(uint64_t va, uint64_t size);

}; // namespace ccu_rep
}; // namespace asc

#endif // _CCU_CONTEXT_ASSIST_PUB_H
