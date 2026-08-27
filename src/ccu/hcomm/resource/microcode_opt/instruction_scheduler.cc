/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS or_op IMPLIED,
 * INCLUDING BUT not_op LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, or_op FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "instruction_scheduler.h"

#include <limits>
#include <unordered_map>

#include "hcomm/common/ccu_log.h"

#include "config/barrier_config.h" // 提供 instr_code_v2 opcode 常量

namespace asc {
namespace ccu_opt {

    namespace {

        inline uint32_t reg_key(const reg_operand& operand)
        {
            return (static_cast<uint32_t>(operand.type) << 16) | static_cast<uint32_t>(operand.reg_id);
        }

        inline ccu_rep::ccu_instr make_nop()
        {
            // ccu_instr 为 POD (header + union, 无非平凡成员), {} 值初始化已将全部字节零化,
            // 无需再 memset_s; 仅设置 nop 的 header 即可.
            ccu_rep::ccu_instr nop_instr{};
            nop_instr.header = ccu_rep::instr_header(instr_code_v2::load_type, instr_code_v2::nop_code);
            return nop_instr;
        }

        // cke_only 顺序调度的可变状态: 输出序列、原->输出映射、统计信息, 以及只跟踪 cke 写者发射
        // cycle 的表. 不做启发式放大, 保证补 nop 有界.
        struct cke_only_state {
            std::vector<ccu_rep::ccu_instr>& out_vec;
            std::vector<int32_t>& orig_to_out;
            scheduler_stats& stats;
            std::unordered_map<uint32_t, int64_t> last_cke_writer_cycle{};
            int64_t cycle = 0;
        };

        inline void emit_nop(cke_only_state& state)
        {
            state.out_vec.push_back(make_nop());
            state.stats.origin_index.push_back(-1); // 无对应源.
            state.stats.nop_inserted++;
            state.cycle++;
        }

        // 计算当前指令为满足 cke 写后读 latency 所需的最早发射 cycle.
        inline int64_t earliest_cke_issue_cycle(const cke_only_state& state, const std::vector<reg_operand>& operands)
        {
            const int64_t cke_latency = static_cast<int64_t>(ccu_rep::ccu_cke_raw_latency);
            int64_t earliest = state.cycle;
            for (const auto& operand : operands) {
                if (operand.is_def || operand.type != reg_type::cke) {
                    continue;
                }
                auto it = state.last_cke_writer_cycle.find(reg_key(operand));
                if (it == state.last_cke_writer_cycle.end()) {
                    continue;
                }
                int64_t needed = it->second + cke_latency;
                if (needed > earliest) {
                    earliest = needed;
                }
            }
            return earliest;
        }

        // 处理单条指令: 先补齐 latency nop, 再原序发射, 最后记录 cke 写者的发射 cycle.
        void schedule_one_cke_instr(cke_only_state& state, const ccu_rep::ccu_instr& instr, size_t origin_idx)
        {
            auto operands = extract_operands_v2(instr);

            int64_t earliest_issue_cycle = earliest_cke_issue_cycle(state, operands);
            while (state.cycle < earliest_issue_cycle) {
                emit_nop(state);
            }

            state.orig_to_out[origin_idx] = static_cast<int32_t>(state.out_vec.size());
            state.out_vec.push_back(instr);
            state.stats.origin_index.push_back(static_cast<int32_t>(origin_idx)); // cke_only 顺序保持.
            int64_t issue_cycle = state.cycle;
            state.cycle++;

            for (const auto& operand : operands) {
                if (!operand.is_def || operand.type != reg_type::cke) {
                    continue;
                }
                state.last_cke_writer_cycle[reg_key(operand)] = issue_cycle;
            }
        }

        // 依据已确定的 out.mission_start_instr_id 重新推导 mission_instr_count:
        // mission 起点落在输出序列内则取到序列尾部的长度, 否则计 0.
        inline void recompute_mission_count(ccu_rep::ccu_instr_info& out, uint16_t start_id)
        {
            if (static_cast<uint32_t>(out.mission_start_instr_id)
                < static_cast<uint32_t>(start_id) + static_cast<uint32_t>(out.instr_count)) {
                out.mission_instr_count = static_cast<uint16_t>(start_id + out.instr_count - out.mission_start_instr_id);
            } else {
                out.mission_instr_count = 0;
            }
        }

        // 顺序扫描后处理: 修正 mission_start_instr_id / mission_instr_count 与 loop / loop_group 引用.
        // cke_only 只在原序上插入 nop, 不重排, 故按 orig_to_out 平移引用即可.
        void fix_references(
            const ccu_rep::ccu_instr_info& input, const std::vector<int32_t>& orig_to_out, ccu_rep::ccu_instr_info& out)
        {
            using namespace instr_code_v2;
            const auto& orig_vec = input.instr_vec;
            const size_t instr_count = orig_vec.size();
            const uint16_t start_id = input.start_instr_id;

            auto remap_global = [&](uint16_t global_id) -> uint16_t {
                // global_id 是"start_id + local_id" 编码的全局 id_, 越界或未映射则原样返回.
                if (global_id < start_id)
                    return global_id;
                uint32_t local_id = static_cast<uint32_t>(global_id) - static_cast<uint32_t>(start_id);
                if (local_id >= orig_to_out.size())
                    return global_id;
                int32_t new_pos = orig_to_out[local_id];
                if (new_pos < 0)
                    return global_id;
                return static_cast<uint16_t>(start_id + new_pos);
            };

            // mission_start_instr_id 修正: 若 mission 起点原本在本序列范围内, 映射到新的位置;
            // mission_instr_count 用序列尾部长度重新推导.
            if (input.mission_start_instr_id >= start_id
                && static_cast<uint32_t>(input.mission_start_instr_id) < static_cast<uint32_t>(start_id) + instr_count) {
                out.mission_start_instr_id = remap_global(input.mission_start_instr_id);
                recompute_mission_count(out, start_id);
            } else {
                out.mission_start_instr_id = input.mission_start_instr_id;
                out.mission_instr_count = input.mission_instr_count;
            }

            for (size_t origin_idx = 0; origin_idx < instr_count; ++origin_idx) {
                const auto& orig_instr = orig_vec[origin_idx];
                int32_t out_pos = orig_to_out[origin_idx];
                if (out_pos < 0)
                    continue;
                auto& out_instr = out.instr_vec[out_pos];
                if (orig_instr.header.type == ctrl_type && orig_instr.header.code == loop_code) {
                    out_instr.v2.loop.start_instr_id = remap_global(orig_instr.v2.loop.start_instr_id);
                    out_instr.v2.loop.end_instr_id = remap_global(orig_instr.v2.loop.end_instr_id);
                } else if (orig_instr.header.type == ctrl_type && orig_instr.header.code == loopgroup_code) {
                    out_instr.v2.loop_group.start_loop_instr_id = remap_global(orig_instr.v2.loop_group.start_loop_instr_id);
                }
            }
        }

    } // namespace

    ccu_rep::ccu_instr_info instruction_scheduler::schedule(const ccu_rep::ccu_instr_info& input)
    {
        return schedule_cke_only(input);
    }

    // cke_only 默认档: 保持原序, 只对 cke 寄存器的写后读 (某条 setcke 写 cke, 之后 waitcke/
    // clearcke 读同一 cke) 按固定 cke latency 补 nop; xn / ms 写后读交由硬件 interlock, 不补
    // 任何 nop. 每个 cke 读者最多补 (L-1) 条 nop, 与"每个 wait 类 rep 预留 L 条"精确对齐.
    ccu_rep::ccu_instr_info instruction_scheduler::schedule_cke_only(const ccu_rep::ccu_instr_info& input)
    {
        stats_ = {};
        const auto& orig_vec = input.instr_vec;
        const size_t instr_count = orig_vec.size();

        std::vector<ccu_rep::ccu_instr> out_vec;
        std::vector<int32_t> orig_to_out;
        out_vec.reserve(instr_count);
        orig_to_out.assign(instr_count, -1);

        // 只跟踪 cke 写者的发射 cycle; 不做启发式放大, 保证补 nop 有界.
        // 索引用 size_t 与 vector::size() 对齐, 避免 instr_count 逼近 65535 时 uint16_t 回绕死循环;
        // 输出条数是否越界预留区由上游 trans_rep_sequence_to_microcode 按 instr_vec.size() 快速失败兜底.
        cke_only_state state{out_vec, orig_to_out, stats_};
        for (size_t i = 0; i < instr_count; ++i) {
            schedule_one_cke_instr(state, orig_vec[i], i);
        }

        // cke_only 不做 BB 切分, 用 1 作为占位 (仅统计意义).
        stats_.basic_blocks = instr_count > 0 ? 1 : 0;

        ccu_rep::ccu_instr_info out;
        out.instr_vec = std::move(out_vec);
        out.start_instr_id = input.start_instr_id;

        // instr_count 字段为 uint16_t. 正常路径下上游按 cke 预留区申请, 优化后条数远小于 65535;
        // 但一旦补 nop 后输出条数超过 uint16_t 上限, 直接截断会让 instr_count 与真实 instr_vec 大小
        // 不一致, 进而使 fix_references 的引用重映射错位. 此处显式记录错误再截断, 把静默数据损坏
        // 变成可观测告警; instr_vec 保留完整大小, 由上游 trans_rep_sequence_to_microcode 按
        // instr_vec.size() > region_size 快速失败兜底 (见 ccu_kernel_mgr.cc).
        constexpr size_t k_max_instr_count = std::numeric_limits<uint16_t>::max();
        if (out.instr_vec.size() > k_max_instr_count) {
            HCCL_ERROR(
                "[instruction_scheduler] optimized instr count_[%zu] exceeds uint16_t range[%zu]; "
                "instr_count field will be truncated, upstream region-size check will reject it.",
                out.instr_vec.size(), k_max_instr_count);
        }
        out.instr_count = static_cast<uint16_t>(out.instr_vec.size());

        fix_references(input, orig_to_out, out);

        return out;
    }

} // namespace ccu_opt
} // namespace asc
