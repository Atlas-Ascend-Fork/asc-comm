/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_MICROCODE_OPT_INSTRUCTION_SCHEDULER_H
#define CCU_MICROCODE_OPT_INSTRUCTION_SCHEDULER_H

#include <cstdint>
#include <vector>

#include "hcomm/resource/microcode/ccu_instr_info_v1.h"
#include "hcomm/resource/microcode_opt/extract_operands.h"

namespace asc {
namespace ccu_opt {

struct scheduler_stats {
    uint32_t nop_removed = 0;     // 输入序列里被剥离的 nop 数 (cke_only 不剥离, 恒 0)
    uint32_t nop_inserted = 0;    // 调度过程因 latency 阻塞而新插入的 nop 数
    uint32_t instr_reordered = 0; // 与输入顺序不同的真实指令数 (cke_only 不重排, 恒 0)
    uint32_t basic_blocks = 0;    // 切分得到的 BB 个数 (cke_only 不切分, 占位为 1)

    // origin_index[i] = 调度后第 i 条指令对应的输入本地下标; -1 表示新插入的填充 nop.
    std::vector<int32_t> origin_index{};
    std::vector<uint16_t> stripped_nop_indices{}; // cke_only 不剥离, 恒为空.
};

// 指令调度算法档位. 极简后端优化只提供 cke_only 一档:
//  * cke_only: 默认档 (唯一档). 完全不重排 / 不剥离, 只识别 cke 寄存器的写后读 (setcke ->
//             waitcke/clearcke) 并按固定 cke latency 补 nop; xn / ms 写后读交由硬件
//             interlock 处理, 不补任何 nop. 配合指令空间预留可保证优化后指令数不越界.
enum class sched_level : uint8_t {
    cke_only = 0,
};

struct instruction_scheduler_options {
    sched_level level = sched_level::cke_only;
};

class instruction_scheduler {
public:
    explicit instruction_scheduler(instruction_scheduler_options opts = {}) : opts_(opts) {}

    ccu_rep::ccu_instr_info schedule(const ccu_rep::ccu_instr_info& input);

    const scheduler_stats& stats() const { return stats_; }

private:
    instruction_scheduler_options opts_;
    scheduler_stats stats_{};

    // cke_only (默认档) 实现: 仅顺序扫描 + 只对 cke 写后读按固定 cke latency 补 nop,
    // xn / ms 写后读不补 (硬件 interlock).
    ccu_rep::ccu_instr_info schedule_cke_only(const ccu_rep::ccu_instr_info& input);
};

} // namespace ccu_opt
} // namespace asc

#endif // CCU_MICROCODE_OPT_INSTRUCTION_SCHEDULER_H
