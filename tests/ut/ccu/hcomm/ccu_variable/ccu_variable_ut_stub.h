/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_VARIABLE_UT_STUB_H
#define CCU_VARIABLE_UT_STUB_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "hcomm/hcomm_ccu_base.h"

namespace asc {

class ccu_kernel;

namespace ut {

// 录制桩单次调用记录：name 为 ccu_kernel 方法名，args 为调用实参（统一转 uint64），
// label 为控制流类接口的标签串（指向宏展开处的字符串字面量，生命周期稳定）。
struct kernel_call {
    std::string name;
    uint64_t args[6] = {0, 0, 0, 0, 0, 0};
    const char* label = nullptr;
};

// 设置/获取当前录制桩 kernel；传 nullptr 可模拟“无 current kernel”错误路径
void set_current_kernel(ccu_kernel* kernel);
ccu_kernel* current_kernel();

// 惰性构造的录制桩 kernel 单例（测试运行期构造，避开静态初始化期依赖）。
// 注意：测试侧不要 include kernel 内部头（其内部 CCU_IF 宏与公开控制流宏冲突），
// 统一通过本接口获取 kernel 指针。
ccu_kernel* recording_kernel();

void clear_calls();
const std::vector<kernel_call>& calls();
std::size_t count_of(const char* name);
// 返回名为 name 的最后一次调用；不存在返回 nullptr
const kernel_call* last_call(const char* name);
// 返回名为 name 的第一次调用；不存在返回 nullptr
const kernel_call* first_call(const char* name);

// 让下一次（仅一次）录制桩方法调用返回指定错误码，覆盖 CCU_THROW_IF_FAILED 抛出路径
void fail_next(CcuResult code);

} // namespace ut
} // namespace asc

#endif // CCU_VARIABLE_UT_STUB_H
