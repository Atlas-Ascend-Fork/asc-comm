/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_MICROCODE_OPT_MICROCODE_OPTIMIZER_H
#define CCU_MICROCODE_OPT_MICROCODE_OPTIMIZER_H

#include <vector>

#include "hcomm/resource/microcode/ccu_instr_info_v1.h"
#include "hcomm/resource/microcode_opt/instruction_scheduler.h"

namespace asc {
namespace ccu_opt {

struct optimizer_stats {
    scheduler_stats sched{};
    // 后端优化耗时 (单位: 微秒 us), 由 optimize() 填充.
    uint64_t pass1_duration_us = 0;
    uint64_t total_duration_us = 0;
};

// 极简后端优化只有 cke_only 一档, 无寄存器重排; 保留结构体仅为对外形态统一.
struct optimizer_options {
    sched_level level = sched_level::cke_only;
};

class microcode_optimizer {
public:
    microcode_optimizer();

    // 档位设置 (sched_level 会同步到 instruction_scheduler_options::level).
    void set_options(const optimizer_options& opts)
    {
        opts_ = opts;
        sched_opts_.level = opts.level;
    }
    const optimizer_options& options() const { return opts_; }

    ccu_rep::ccu_instr_info optimize(const ccu_rep::ccu_instr_info& input);

    const optimizer_stats& stats() const { return stats_; }

    // 编译期默认档位: 恒为 cke_only (极简后端优化无其他档位).
    static optimizer_options default_options();

    // 后端优化统一入口: V2 场景无条件启用 (V1 由调用方保证不进来).
    // reserve_xn_id / reserve_cke_id 为 translator 保留寄存器, cke_only 不重命名寄存器,
    // 故这两个参数当前仅用于日志观测.
    static ccu_rep::ccu_instr_info run(
        const ccu_rep::ccu_instr_info& input, uint16_t reserve_xn_id, uint16_t reserve_cke_id);

private:
    // dfx / opt_log: 把"优化前指令序列 + 优化后指令序列 + 后→前指令下标映射"一次性写入
    // HCCL_INFO, 供用户结合硬件 runlog 定位错误来源.
    void dump_opt_log(const ccu_rep::ccu_instr_info& input, const ccu_rep::ccu_instr_info& output) const;

    instruction_scheduler_options sched_opts_;
    optimizer_options opts_;
    optimizer_stats stats_{};
};

} // namespace ccu_opt
} // namespace asc

#endif // CCU_MICROCODE_OPT_MICROCODE_OPTIMIZER_H
