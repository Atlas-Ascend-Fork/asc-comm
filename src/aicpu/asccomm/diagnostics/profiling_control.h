/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef ASCCOMM_PROFILING_CONTROL_H
#define ASCCOMM_PROFILING_CONTROL_H
#include <atomic>
#include <cstdint>
#include "prof_common.h"
#include "aprof_pub.h"
namespace Asc {
// Independent of HCOMM. Public profapi constants define the control contract.
class ProfilingControl {
public:
    enum : uint32_t { L0 = 1U, L1 = 2U };
    using FeatureQuery = int32_t (*)(uint64_t);
    void Command(const MsprofCommandHandle& command)
    {
        if (command.type == PROF_COMMANDHANDLE_TYPE_START) {
            uint32_t bits = 0;
            if (command.profSwitch & ADPROF_TASK_TIME_L0) {
                bits |= L0;
            }
            if (command.profSwitch & ADPROF_TASK_TIME_L1) {
                bits |= L1;
            }
            mask_.fetch_or(bits, std::memory_order_acq_rel);
        } else if (command.type == PROF_COMMANDHANDLE_TYPE_STOP) {
            mask_.store(0, std::memory_order_release);
        }
    }
    uint32_t Refresh(bool hasMsprofBatch, FeatureQuery query)
    {
        if (!hasMsprofBatch) {
            uint32_t bits = 0;
            if (query != nullptr) {
                if (query(ADPROF_TASK_TIME_L0) > 0) {
                    bits |= L0;
                }
                if (query(ADPROF_TASK_TIME_L1) > 0) {
                    bits |= L1;
                }
            }
            mask_.store(bits, std::memory_order_release);
        }
        return Mask();
    }
    uint32_t Mask() const { return mask_.load(std::memory_order_acquire); }

private:
    std::atomic<uint32_t> mask_{0};
};
} // namespace Asc
#endif
