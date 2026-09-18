/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "microcode_optimizer.h"

#include <chrono>
#include <sstream>
#include <string>

#include "hcomm/common/ccu_log.h"
#include "hcomm/resource/microcode/ccu_microcode_v1.h"

namespace asc {
namespace ccu_opt {

microcode_optimizer::microcode_optimizer() = default;

optimizer_options microcode_optimizer::default_options()
{
    // 极简后端优化恒为 cke_only (只处理 cke 写后读), 无其他档位, 运行时不可改.
    optimizer_options opts{};
    opts.level = sched_level::cke_only;
    return opts;
}

ccu_rep::ccu_instr_info microcode_optimizer::optimize(const ccu_rep::ccu_instr_info& input)
{
    sched_opts_.level = opts_.level;

    using Clock = std::chrono::steady_clock;
    auto us_since = [](Clock::time_point start_time) -> uint64_t {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start_time).count());
    };

    const auto total_start = Clock::now();

    const auto pass1_start = Clock::now();
    instruction_scheduler sched(sched_opts_);
    ccu_rep::ccu_instr_info after_sched = sched.schedule(input);
    stats_.sched = sched.stats();
    stats_.pass1_duration_us = us_since(pass1_start);
    stats_.total_duration_us = us_since(total_start);

    HCCL_INFO(
        "[ccu_microcode_opt][timing] total=%lluus, pass1(sched)=%lluus",
        static_cast<unsigned long long>(stats_.total_duration_us),
        static_cast<unsigned long long>(stats_.pass1_duration_us));

    dump_opt_log(input, after_sched);
    return after_sched;
}

namespace {

// 打印输入序列 (用户视角的原始指令). 全局 id_ = start_instr_id + local, 与硬件视角一致.
void dump_opt_log_input(const ccu_rep::ccu_instr_info& input)
{
    const uint16_t in_start = input.start_instr_id;
    for (size_t i = 0; i < input.instr_vec.size(); ++i) {
        HCCL_INFO(
            "[ccu_microcode_opt][optlog] input   [%3zu] gid=%u: %s", i, static_cast<unsigned>(in_start + i),
            ccu_rep::parse_instr(input.instr_vec.data() + i).c_str());
    }
}

// 打印输出序列 + 后→前映射. runlog 里报"指令 gid=X 挂了", 用户在此段直接搜 gid=X 即可
// 定位到 src_ 行, src_=-1 表示是新插入的 nop.
void dump_opt_log_output(
    const ccu_rep::ccu_instr_info& input, const ccu_rep::ccu_instr_info& output,
    const std::vector<int32_t>& origin_index, bool map_consistent)
{
    const uint16_t in_start = input.start_instr_id;
    const uint16_t out_start = output.start_instr_id;
    for (size_t i = 0; i < output.instr_vec.size(); ++i) {
        const char* src_tag = "NEW  (inserted nop)";
        std::string src_buf;
        if (map_consistent) {
            int32_t src_ = origin_index[i];
            if (src_ >= 0) {
                std::ostringstream oss;
                oss << "src_[" << src_ << "] gid=" << (in_start + static_cast<uint32_t>(src_));
                src_buf = oss.str();
                src_tag = src_buf.c_str();
            }
        } else {
            src_tag = "N/A (mapping omitted)";
        }
        HCCL_INFO(
            "[ccu_microcode_opt][optlog] output  [%3zu] gid=%u <- %s : %s", i, static_cast<unsigned>(out_start + i),
            src_tag, ccu_rep::parse_instr(output.instr_vec.data() + i).c_str());
    }
}

} // namespace

void microcode_optimizer::dump_opt_log(
    const ccu_rep::ccu_instr_info& input, const ccu_rep::ccu_instr_info& output) const
{
    const auto& origin_index = stats_.sched.origin_index;
    const bool map_consistent = origin_index.size() == output.instr_vec.size();
    if (!map_consistent) {
        HCCL_WARNING(
            "[ccu_microcode_opt][optlog] origin index size mismatch: "
            "origin_index=%zu, output=%zu; mapping will be omitted.",
            origin_index.size(), output.instr_vec.size());
    }

    HCCL_INFO(
        "[ccu_microcode_opt][optlog] === begin "
        "(input_instr_count=%u, output_instr_count=%u, "
        "mission_start before=%u after=%u) ===",
        input.instr_count, output.instr_count, input.mission_start_instr_id, output.mission_start_instr_id);

    dump_opt_log_input(input);
    dump_opt_log_output(input, output, origin_index, map_consistent);

    HCCL_INFO("[ccu_microcode_opt][optlog] === end ===");
}

ccu_rep::ccu_instr_info microcode_optimizer::run(
    const ccu_rep::ccu_instr_info& input, uint16_t reserve_xn_id, uint16_t reserve_cke_id)
{
    optimizer_options opts = default_options(); // 恒 cke_only.
    HCCL_INFO(
        "[ccu_microcode_opt] active options: sched=cke_only, reserve_xn=%u, reserve_cke=%u",
        static_cast<unsigned>(reserve_xn_id), static_cast<unsigned>(reserve_cke_id));

    microcode_optimizer opt;
    opt.set_options(opts);
    return opt.optimize(input);
}

} // namespace ccu_opt
} // namespace asc
