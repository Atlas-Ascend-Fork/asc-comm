/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
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
    // 无需再 memset_s; 仅设置 NOP 的 header 即可.
    ccu_rep::ccu_instr nop_instr{};
    nop_instr.header = ccu_rep::instr_header(instr_code_v2::load_type, instr_code_v2::nop_code);
    return nop_instr;
}

// rel_jmp 模板内部使用的固定常量 (与 ccu_microcode_v2.cc 同源, 见 rel_jmp 生成器):
//   两条内部 jump 的固定跳距 (IF 分支跳 5 / NOP 分支跳 3), 及两种 condition_type.
namespace rel_jmp_const {
constexpr uint16_t JMP_TARGET_PC_IF_BRANCH = 5;  // P+1 load_imd_to_xn(xn1, 5)
constexpr uint16_t JMP_TARGET_PC_NOP_BRANCH = 3; // P+5 load_imd_to_xn(xn1, 3)
constexpr uint16_t JMP_COND_TYPE_IF = 2;         // P+2 换算 jump 的 condition_type
constexpr uint16_t JMP_COND_TYPE_UNCOND = 6;     // P+6 无条件 jump 的 condition_type
constexpr int TEMPLATE_LEN = 9;                  // rel_jmp 展开固定 9 条
} // namespace rel_jmp_const

// rel_jmp 模板解析结果. matched=true 时各下标字段有效.
struct rel_jmp_match {
    bool matched = false;
    size_t p0 = 0;          // 块起始 (load_imd_to_xn(xn0, jmp_instr_id))
    size_t inner_jmp = 0;   // P+2 换算 jump
    size_t p3 = 0;          // P+3 load_imd_to_xn(xn0, 0x10000 - jmp_instr_id)
    uint16_t xn0 = 0;       // jmp_instr_id 基准寄存器
    uint16_t xn1 = 0;       // 模板内固定跳距寄存器
    uint16_t target_xn = 0; // 目标绝对 id 寄存器 (被 add/sub 换算)
};

inline bool is_load_imd(const ccu_rep::ccu_instr& instr)
{
    return instr.header.type == instr_code_v2::load_type && instr.header.code == instr_code_v2::loadimdtox_code;
}
inline bool is_ctrl_jmp(const ccu_rep::ccu_instr& instr)
{
    return instr.header.type == instr_code_v2::ctrl_type && instr.header.code == instr_code_v2::jmp_code;
}
inline bool is_arith(const ccu_rep::ccu_instr& instr, uint16_t code)
{
    return instr.header.type == instr_code_v2::load_type && instr.header.code == code;
}

// rel_jmp 模板寄存器角色: 由 P+2 换算 jump 解出的三个寄存器 id, 供逐条校验共享.
struct rel_jmp_regs {
    uint16_t xn0 = 0;       // jmp_instr_id 基准寄存器
    uint16_t xn1 = 0;       // 模板内固定跳距寄存器
    uint16_t target_xn = 0; // 目标绝对 id 寄存器
};

// P+4 ADD / P+7 SUB 共享校验: 算术码 + xd/xn==target_xn + xm==xn0.
inline bool is_rel_jmp_arith(const ccu_rep::ccu_instr& instr, uint16_t code, const rel_jmp_regs& regs)
{
    return is_arith(instr, code) && instr.v2.operate.xd_id == regs.target_xn &&
           instr.v2.operate.xn_id == regs.target_xn && instr.v2.operate.xm_id == regs.xn0;
}

// 校验 P+0..P+1 (P+2 前两条): P+1 load_imd_to_xn(xn1, 5), P+0 load_imd_to_xn(xn0, *).
inline bool check_rel_jmp_head(
    const std::vector<ccu_rep::ccu_instr>& vec, size_t inner_jmp_idx, const rel_jmp_regs& regs)
{
    using namespace rel_jmp_const;
    const auto& p1 = vec[inner_jmp_idx - 1];
    const auto& p0 = vec[inner_jmp_idx - 2];
    if (!is_load_imd(p1) || p1.v2.load_imd_to_x.xn_id != regs.xn1 ||
        p1.v2.load_imd_to_x.immediate != JMP_TARGET_PC_IF_BRANCH) {
        return false;
    }
    return is_load_imd(p0) && p0.v2.load_imd_to_x.xn_id == regs.xn0;
}

// 校验 P+3..P+7 (P+2 后五条): P+3 load_imd(xn0), P+4 add, P+5 load_imd(xn1,3), P+6 UNCOND jump, P+7 sub.
inline bool check_rel_jmp_tail(
    const std::vector<ccu_rep::ccu_instr>& vec, size_t inner_jmp_idx, const rel_jmp_regs& regs)
{
    using namespace instr_code_v2;
    using namespace rel_jmp_const;
    const auto& p3 = vec[inner_jmp_idx + 1];
    const auto& p4 = vec[inner_jmp_idx + 2];
    const auto& p5 = vec[inner_jmp_idx + 3];
    const auto& p6 = vec[inner_jmp_idx + 4];
    const auto& p7 = vec[inner_jmp_idx + 5];
    if (!is_load_imd(p3) || p3.v2.load_imd_to_x.xn_id != regs.xn0) {
        return false;
    }
    if (!is_rel_jmp_arith(p4, instr_code_v2::add_code, regs)) {
        return false;
    }
    if (!is_load_imd(p5) || p5.v2.load_imd_to_x.xn_id != regs.xn1 ||
        p5.v2.load_imd_to_x.immediate != JMP_TARGET_PC_NOP_BRANCH) {
        return false;
    }
    if (!is_ctrl_jmp(p6) || p6.v2.jmp.condition_type != JMP_COND_TYPE_UNCOND ||
        p6.v2.jmp.rel_tar_instr_xn_id != regs.xn1) {
        return false;
    }
    return is_rel_jmp_arith(p7, instr_code_v2::sub_code, regs);
}

// 强指纹校验: 以 inner_jmp_idx (候选 P+2 换算 jump) 为锚, 校验其所在的 9 条是否构成完整 rel_jmp
// 模板. 校验点 (任一不满足即判否), 互锁性极强, 普通条件跳转不可能全中:
//   P+2 vec[i]      : CTRL/JMP, condition_type==IF(2); 取 xn1=rel_tar, target_xn=condition, xn0=expected
//   P+1 vec[i-1]    : LOADIMDTOX, xn_id==xn1, immediate==5 (IF 分支固定跳距)
//   P+0 vec[i-2]    : LOADIMDTOX, xn_id==xn0
//   P+3 vec[i+1]    : LOADIMDTOX, xn_id==xn0 (immediate 应为 0x10000 - P+0.immediate)
//   P+4 vec[i+2]    : ADD, xd_id==target_xn, xn_id==target_xn, xm_id==xn0
//   P+5 vec[i+3]    : LOADIMDTOX, xn_id==xn1, immediate==3 (NOP 分支固定跳距)
//   P+6 vec[i+4]    : CTRL/JMP, condition_type==UNCOND(6), rel_tar==xn1
//   P+7 vec[i+5]    : SUB, xd_id==target_xn, xn_id==target_xn, xm_id==xn0
// 普通条件跳转 (EQ/NE/GT/... + 用户 expected/condition) 因固定跳距 5/3、成对 add/sub、配套第二跳
// 缺一即被排除, 从根本上杜绝把 "expected 比较值 load" 误判为 "jmp_instr_id 基准 load".
rel_jmp_match match_rel_jmp_template(const std::vector<ccu_rep::ccu_instr>& vec, size_t inner_jmp_idx)
{
    using namespace rel_jmp_const;
    rel_jmp_match match;

    // 边界: inner_jmp 至少是 P+2, 其后还需 P+3..P+7 (5 条).
    if (inner_jmp_idx < 2 || inner_jmp_idx + 5 >= vec.size()) {
        return match;
    }
    const auto& jmp = vec[inner_jmp_idx];
    if (!is_ctrl_jmp(jmp) || jmp.v2.jmp.condition_type != JMP_COND_TYPE_IF) {
        return match;
    }
    const rel_jmp_regs regs{jmp.v2.jmp.expected_xn_id, jmp.v2.jmp.rel_tar_instr_xn_id, jmp.v2.jmp.condition_xn_id};

    if (!check_rel_jmp_head(vec, inner_jmp_idx, regs) || !check_rel_jmp_tail(vec, inner_jmp_idx, regs)) {
        return match;
    }

    match.matched = true;
    match.p0 = inner_jmp_idx - 2;
    match.inner_jmp = inner_jmp_idx;
    match.p3 = inner_jmp_idx + 1;
    match.xn0 = regs.xn0;
    match.xn1 = regs.xn1;
    match.target_xn = regs.target_xn;
    return match;
}

// 识别 rel_jmp 原子块 (func-call / func-ret 运行期地址跳转), 返回逐指令保护掩码.
// 用 match_rel_jmp_template 强指纹匹配: 命中的块为 [P+0, P+8] 共 9 条, 全部标记为块内禁止插 NOP
// (块内固定跳距 5/3 与 P+2 相对 P+0 的换算关系依赖块内不被撕裂). 目标区间的距离修正由
// fix_rel_jmp_func 在 fix_references 阶段完成, 不在此保护.
std::vector<bool> mark_rel_jmp_protected_ranges(const std::vector<ccu_rep::ccu_instr>& vec)
{
    using namespace rel_jmp_const;
    const size_t count = vec.size();
    std::vector<bool> mask(count, false);
    for (size_t i = 0; i < count; ++i) {
        if (!is_ctrl_jmp(vec[i])) {
            continue;
        }
        rel_jmp_match match = match_rel_jmp_template(vec, i);
        if (!match.matched) {
            continue;
        }
        // 块 = [P+0, P+8] = [match.p0, match.p0 + 8].
        const size_t block_end = match.p0 + static_cast<size_t>(TEMPLATE_LEN) - 1;
        for (size_t k = match.p0; k <= block_end && k < count; ++k) {
            mask[k] = true;
        }
    }
    return mask;
}

// cke_only 顺序调度的可变状态: 输出序列、原->输出映射、统计信息, 以及只跟踪 CKE 写者发射
// cycle 的表. 不做启发式放大, 保证补 NOP 有界.
struct cke_only_state {
    std::vector<ccu_rep::ccu_instr>& out_vec;
    std::vector<int32_t>& orig_to_out;
    scheduler_stats& stats;
    const std::vector<bool>& rel_jmp_protected; // 逐指令保护掩码: true 表示属于 rel_jmp 原子块, 块内禁止插 NOP.
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

// 计算当前指令为满足 CKE 写后读 latency 所需的最早发射 cycle.
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

// 处理单条指令: 先补齐 latency NOP, 再原序发射, 最后记录 CKE 写者的发射 cycle.
void schedule_one_cke_instr(cke_only_state& state, const ccu_rep::ccu_instr& instr, size_t origin_idx)
{
    auto operands = extract_operands_v2(instr);

    // rel_jmp 原子块内禁止插 NOP: 块内指令(load/jump/add/sub/nop)不产生 CKE 写者, 也不含 CKE 读者,
    // earliest_issue_cycle 恒等于当前 cycle, 正常路径本就不会补 NOP; 这里显式跳过补 NOP 是防御, 确保
    // 即使块紧邻的 CKE 写者仍有残余 latency 需求, 也不会把 NOP 插进/插到块中间破坏运行期地址链.
    const bool is_protected = origin_idx < state.rel_jmp_protected.size() && state.rel_jmp_protected[origin_idx];
    if (!is_protected) {
        int64_t earliest_issue_cycle = earliest_cke_issue_cycle(state, operands);
        while (state.cycle < earliest_issue_cycle) {
            emit_nop(state);
        }
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
    if (static_cast<uint32_t>(out.mission_start_instr_id) <
        static_cast<uint32_t>(start_id) + static_cast<uint32_t>(out.instr_count)) {
        out.mission_instr_count = static_cast<uint16_t>(start_id + out.instr_count - out.mission_start_instr_id);
    } else {
        out.mission_instr_count = 0;
    }
}

// 与 get_relative_instr_id / jump_executor 的回绕空间一致. 用 uint64_t 承载, 使所有涉及 immediate
// (字段本身为 uint64_t, 可存完整 64 位业务立即数) 的读取/运算全程 64 位, 杜绝中间隐式截断.
constexpr uint64_t k_instr_id_space = 0x10000ULL;

// 普通相对跳转 offset 修正.
//
// 背景: v2 的 jmp 目标不是直接写在 jmp 指令里的绝对 instr_id, 而是"相对距离":
// 生成端在紧邻 jmp 之前用一条 load_imd_to_xn 把 offset = target - jmp_pc 加载进 rel_tar_instr_xn_id,
// 硬件按 next_ins_idx = jmp_pc + offset (mod 0x10000) 跳转 (见 jump_executor.cc 相对跳转分支).
// 因此只要在 jmp 与目标之间净插入了 k 条 NOP, 真实相对距离就变了, 而 offset 立即数是编译期
// 写死的, 不修正会跳错. loop/loop_group 用绝对 id 靠 remap_global 平移, jmp 则必须改写 offset.
// 向前找最近一条写 tgt_xn 的指令: 命中 load_imd_to_xn 返回其原始下标; 命中算术等其它写者则告警并
// 返回 -1 (放弃修正); 找不到任何写者也返回 -1 (保守不动).
int32_t find_offset_loader_orig_idx(
    const std::vector<ccu_rep::ccu_instr>& orig_vec, size_t jmp_orig_idx, uint16_t tgt_xn)
{
    using namespace instr_code_v2;
    for (int32_t j = static_cast<int32_t>(jmp_orig_idx) - 1; j >= 0; --j) {
        const auto& cur = orig_vec[j];
        if (cur.header.type == instr_code_v2::load_type && cur.header.code == instr_code_v2::loadimdtox_code &&
            cur.v2.load_imd_to_x.xn_id == tgt_xn) {
            return j;
        }
        for (const auto& op : extract_operands_v2(cur)) {
            if (op.is_def && op.type == reg_type::xn && op.reg_id == tgt_xn) {
                HCCL_WARNING(
                    "[instruction_scheduler] jmp@%zu target reg X%u overwritten by non-imm instr@%d; "
                    "skip relative-offset fix.",
                    jmp_orig_idx, static_cast<unsigned>(tgt_xn), j);
                return -1;
            }
        }
    }
    return -1; // 没找到立即数来源, 保守不动.
}

void fix_plain_jump_offset(
    const std::vector<ccu_rep::ccu_instr>& orig_vec, const std::vector<int32_t>& orig_to_out, size_t jmp_orig_idx,
    std::vector<ccu_rep::ccu_instr>& out_vec)
{
    const uint16_t tgt_xn = orig_vec[jmp_orig_idx].v2.jmp.rel_tar_instr_xn_id;

    const int32_t load_orig_idx = find_offset_loader_orig_idx(orig_vec, jmp_orig_idx, tgt_xn);
    if (load_orig_idx < 0) {
        return;
    }

    const int32_t load_out_pos = orig_to_out[load_orig_idx];
    const int32_t jmp_out_pos = orig_to_out[jmp_orig_idx];
    if (load_out_pos < 0 || jmp_out_pos < 0) {
        return; // 理论上二者都保留 (cke_only 不删指令), 兜底防御.
    }

    const uint64_t old_offset = out_vec[load_out_pos].v2.load_imd_to_x.immediate;
    // 定位护栏 (非落点校验): offset 语义上是相对距离, 合法域即 [0, 0x10000). 向前扫描找 offset
    // loader 是启发式的, 若读到 >= 0x10000, 说明大概率误命中了往同一寄存器装 64 位业务数据的
    // load (而非真正的 offset loader). 此时改写会截断高位破坏业务数据 —— 放弃修正并告警.
    // 注: 这里判的是"偏移是否超出该字段合法域", 落点是否合法由下方 orig_target_idx 越界检查负责.
    if (old_offset >= k_instr_id_space) {
        HCCL_WARNING(
            "[instruction_scheduler] jmp@%zu offset-loader immediate %llu out of relative-offset range "
            "[0,0x10000); likely mismatched a 64-bit value load, skip fix to avoid truncation.",
            jmp_orig_idx, static_cast<unsigned long long>(old_offset));
        return;
    }
    // 旧 offset 基准是 jmp 原始位置, 反推原始目标下标 (回绕). 全程 uint64_t.
    const uint64_t orig_target_idx = (static_cast<uint64_t>(jmp_orig_idx) + old_offset) % k_instr_id_space;
    if (orig_target_idx >= orig_to_out.size() || orig_to_out[orig_target_idx] < 0) {
        HCCL_WARNING(
            "[instruction_scheduler] jmp@%zu old offset %llu points outside sequence (target idx %llu); "
            "skip relative-offset fix.",
            jmp_orig_idx, static_cast<unsigned long long>(old_offset),
            static_cast<unsigned long long>(orig_target_idx));
        return;
    }

    const uint64_t new_target_pos = static_cast<uint64_t>(orig_to_out[orig_target_idx]);
    const uint64_t new_jmp_pos = static_cast<uint64_t>(jmp_out_pos);
    const uint64_t new_offset = (new_target_pos + k_instr_id_space - new_jmp_pos) % k_instr_id_space;
    out_vec[load_out_pos].v2.load_imd_to_x.immediate = new_offset;
}

// rel_jmp (func-call / func-ret 运行期地址跳转) 修正.
//
// rel_jmp 用绝对量表达跳转: 运行时寄存器换算值 = target_abs_id - jmp_instr_id, 主 jump (紧随 9 条模板
// 之后) 按 next_pc = 主jump_pc + 换算值 跳转; 生成端令 jmp_instr_id == 主jump_pc, 故最终 next_pc ==
// target_abs_id. 插 NOP 后主 jump 与目标各自位移, 二者相对距离改变, 必须把两个绝对量分别重映射:
//   * jmp_instr_id (rel_jmp 模板 P+0 load_imd_to_xn(xn0, jmp_instr_id) 与 P+3 load_imd_to_xn(xn0,
//     0x10000 - jmp_instr_id)): jmp_instr_id == 主 jump 旧 PC, 平移到主 jump 新 PC (= remap(jmp_instr_id)).
//   * 目标绝对 id (由加载 target_xn 的 load_imd_to_xn 提供): remap 到新位置; 若目标由 add 加载
//     (func_addr_var 运行期变量, 外部绝对地址), 不受本段插 NOP 影响, 不动.
// 块内 9 条由 mark_rel_jmp_protected_ranges 保证不被 NOP 撕裂, 故模板内固定跳距 (5 / 3) 无需修改.
//
// 定位方式: P+0 / P+3 由已通过强指纹校验的 rel_jmp_match 按模板固定偏移直接给出 (match.p0 / match.p3),
// 不再靠"解释 jmp 的 expected_xn_id 字段 + 向前扫描"猜测 —— 后者在普通条件跳转上会误命中 expected
// 比较值 load. 只有整段构成 rel_jmp 模板才会走到这里, 故 match.p0 / match.p3 必为 jmp_instr_id 基准 load.
//
// rel_jmp 目标绝对 id 的 remap 改写: 命中"编译期常量绝对 id"的 load_imd_to_xn 后, 越界告警 / 否则 remap 平移.
// 从 fix_rel_jmp_func 拆出, 消除 for -> if -> if -> if/else -> 赋值 的过深嵌套 (超大深度函数告警).
//
// 硬件约束: jmp 落点绝对 PC 必须落在指令空间 [0, 0x10000). 这里 raw_target 是"目标绝对 instr_id"
// (由生成端 load_imd_to_xn(target_xn, func_block->start_instr_id()/func_ret.id()) 装入编译期常量绝对 id;
// 运行期偏移由模板 P+4 add(0x10000 - jmp_instr_id) 现算, 不落在本立即数里). 故越界校验分两处:
//   * 旧绝对 id (raw_target): 强指纹已保证 < 0x10000, 越界说明数据异常, 放弃改写;
//   * remap 后新绝对 id (new_target): 插 NOP 后 PC 整体后移, 显式校验新落点仍在指令空间内.
template <typename remap_fn>
void remap_rel_jmp_target_immediate(
    const rel_jmp_match& match, int32_t out_pos, std::vector<ccu_rep::ccu_instr>& out_vec, remap_fn remap_id)
{
    const uint64_t raw_target = out_vec[out_pos].v2.load_imd_to_x.immediate;
    if (raw_target >= k_instr_id_space) {
        HCCL_WARNING(
            "[instruction_scheduler] rel_jmp@%zu: target absolute id %llu exceeds instr_id space; "
            "skip target remap.",
            match.inner_jmp, static_cast<unsigned long long>(raw_target));
        return;
    }
    const uint64_t new_target = static_cast<uint64_t>(remap_id(static_cast<uint16_t>(raw_target)));
    if (new_target >= k_instr_id_space) {
        HCCL_WARNING(
            "[instruction_scheduler] rel_jmp@%zu: remapped target absolute id %llu exceeds instr_id space "
            "after NOP insertion; skip target remap.",
            match.inner_jmp, static_cast<unsigned long long>(new_target));
        return;
    }
    out_vec[out_pos].v2.load_imd_to_x.immediate = new_target;
}

// rel_jmp (func-call / func-ret 运行期地址跳转) 专用修正. 与普通相对跳转 (fix_plain_jump_offset)
// 区分命名: 本函数处理的 P+0/target 立即数是"编译期常量绝对 instr_id", 运行期相对偏移由模板
// add/sub 现算; 普通相对跳转处理的是直接写死的相对偏移立即数.
//
// remap_id: 把"旧全局 instr_id"映射到"新全局 instr_id"(与 fix_references 的 remap_global 同语义).
template <typename remap_fn>
void fix_rel_jmp_func(
    const std::vector<ccu_rep::ccu_instr>& orig_vec, const std::vector<int32_t>& orig_to_out,
    const rel_jmp_match& match, std::vector<ccu_rep::ccu_instr>& out_vec, remap_fn remap_id)
{
    using namespace instr_code_v2;

    // 1) 修 jmp_instr_id 基准: P+0 (match.p0, 立即数 jmp_instr_id) 与 P+3 (match.p3, 立即数 0x10000 - jmp_instr_id).
    const int32_t p0_out = orig_to_out[match.p0];
    const int32_t p3_out = orig_to_out[match.p3];
    if (p0_out < 0 || p3_out < 0) {
        return; // 块内不删指令, 兜底防御.
    }
    const uint64_t raw_jmp_instr_id = out_vec[p0_out].v2.load_imd_to_x.immediate;
    if (raw_jmp_instr_id >= k_instr_id_space) {
        // jmp_instr_id 是主 jump 的绝对 instr_id (< 0x10000). 强指纹已确保这是 rel_jmp, 正常不会越界;
        // 越界则说明数据异常, 放弃并告警, 不做可能截断的改写.
        HCCL_WARNING(
            "[instruction_scheduler] rel_jmp@%zu: base absolute id %llu exceeds instr_id space; skip fix.",
            match.inner_jmp, static_cast<unsigned long long>(raw_jmp_instr_id));
        return;
    }
    const uint64_t new_jmp_instr_id = static_cast<uint64_t>(remap_id(static_cast<uint16_t>(raw_jmp_instr_id)));
    // 硬件约束: 主 jump 落点绝对 PC 必须落在指令空间内. 插 NOP 后 PC 整体后移, 显式校验新绝对
    // id 仍 < 0x10000, 越界则放弃改写 (避免写出会被硬件回绕到错误 PC 的基准值).
    if (new_jmp_instr_id >= k_instr_id_space) {
        HCCL_WARNING(
            "[instruction_scheduler] rel_jmp@%zu: remapped base absolute id %llu exceeds instr_id space "
            "after NOP insertion; skip fix.",
            match.inner_jmp, static_cast<unsigned long long>(new_jmp_instr_id));
        return;
    }
    out_vec[p0_out].v2.load_imd_to_x.immediate = new_jmp_instr_id;
    out_vec[p3_out].v2.load_imd_to_x.immediate = k_instr_id_space - new_jmp_instr_id;

    // 2) 修目标绝对 id: 向前找加载 target_xn 的指令 (在块之前, 位置不固定, 但 target_xn 由强指纹给出).
    //    load_imd_to_xn(xn_id==target_xn) -> 目标是编译期常量绝对 id, remap 平移;
    //    add/sub(xd_id==target_xn)     -> 目标是运行期变量 (func_addr_var, 外部绝对地址), 不动.
    for (int32_t j = static_cast<int32_t>(match.p0) - 1; j >= 0; --j) {
        const auto& cur = orig_vec[j];
        if (cur.header.type == instr_code_v2::load_type && cur.header.code == instr_code_v2::loadimdtox_code &&
            cur.v2.load_imd_to_x.xn_id == match.target_xn) {
            if (orig_to_out[j] >= 0) {
                remap_rel_jmp_target_immediate(match, orig_to_out[j], out_vec, remap_id);
            }
            break;
        }
        if (cur.header.type == instr_code_v2::load_type &&
            (cur.header.code == instr_code_v2::add_code || cur.header.code == instr_code_v2::sub_code) &&
            cur.v2.operate.xd_id == match.target_xn) {
            // 目标为运行期变量 (外部绝对地址), 不随本段插 NOP 变化, 无需修正.
            break;
        }
    }
}

// 处理单条 JMP 指令的引用修正 (从 fix_references 主循环拆出, 降低单函数体量与圈复杂度).
template <typename remap_fn>
void fix_one_jump_reference(
    const ccu_rep::ccu_instr_info& input, const std::vector<int32_t>& orig_to_out,
    const std::vector<bool>& rel_jmp_protected, size_t origin_idx, ccu_rep::ccu_instr_info& out, remap_fn remap_global)
{
    const auto& orig_vec = input.instr_vec;
    const auto& orig_instr = orig_vec[origin_idx];
    rel_jmp_match rel_jmp = match_rel_jmp_template(orig_vec, origin_idx);
    if (orig_instr.v2.jmp.jump_mode != 0) {
        // 绝对跳转 (jump_mode == 1): 目标是绝对 instr_id, 需按绝对 id 重映射, 与相对跳转不同.
        // 当前生成端从不产生绝对跳转 (ccu_v2::jump 恒留 jump_mode=0), 不做猜测性改写, 显式告警.
        HCCL_ERROR(
            "[instruction_scheduler] jmp@%zu is absolute (jump_mode=1); unsupported, jump target may be "
            "wrong after NOP insertion.",
            origin_idx);
    } else if (rel_jmp.matched) {
        // rel_jmp 换算 jump (P+2, 强指纹命中): 目标用绝对量表达, 按模板固定偏移修 jmp_instr_id
        // 基准 (P+0/P+3) + 目标绝对 id, 不触碰任何 expected/condition 业务 load.
        fix_rel_jmp_func(orig_vec, orig_to_out, rel_jmp, out.instr_vec, remap_global);
    } else if (origin_idx < rel_jmp_protected.size() && rel_jmp_protected[origin_idx]) {
        // rel_jmp 模板内的其它 jmp (P+6 无条件跳): 跳距是模板内固定常量, 块内不插 NOP, 不改.
    } else {
        // 普通相对跳转: 改写其前置 load_imd_to_xn 的 offset 立即数, 而非 jmp 指令本身.
        fix_plain_jump_offset(orig_vec, orig_to_out, origin_idx, out.instr_vec);
    }
}

// 顺序扫描后处理: 修正 mission_start_instr_id / mission_instr_count 与 loop / loop_group 引用.
// cke_only 只在原序上插入 NOP, 不重排, 故按 orig_to_out 平移引用即可.
void fix_references(
    const ccu_rep::ccu_instr_info& input, const std::vector<int32_t>& orig_to_out,
    const std::vector<bool>& rel_jmp_protected, ccu_rep::ccu_instr_info& out)
{
    using namespace instr_code_v2;
    const auto& orig_vec = input.instr_vec;
    const size_t instr_count = orig_vec.size();
    const uint16_t start_id = input.start_instr_id;

    auto remap_global = [&](uint16_t global_id) -> uint16_t {
        // global_id 是"start_id + local_id" 编码的全局 id, 越界或未映射则原样返回.
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
    if (input.mission_start_instr_id >= start_id &&
        static_cast<uint32_t>(input.mission_start_instr_id) < static_cast<uint32_t>(start_id) + instr_count) {
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
        if (orig_instr.header.type == instr_code_v2::ctrl_type && orig_instr.header.code == instr_code_v2::loop_code) {
            out_instr.v2.loop.start_instr_id = remap_global(orig_instr.v2.loop.start_instr_id);
            out_instr.v2.loop.end_instr_id = remap_global(orig_instr.v2.loop.end_instr_id);
        } else if (
            orig_instr.header.type == instr_code_v2::ctrl_type &&
            orig_instr.header.code == instr_code_v2::loopgroup_code) {
            out_instr.v2.loop_group.start_loop_instr_id = remap_global(orig_instr.v2.loop_group.start_loop_instr_id);
        } else if (
            orig_instr.header.type == instr_code_v2::ctrl_type && orig_instr.header.code == instr_code_v2::jmp_code) {
            fix_one_jump_reference(input, orig_to_out, rel_jmp_protected, origin_idx, out, remap_global);
        }
    }
}

} // namespace

ccu_rep::ccu_instr_info instruction_scheduler::schedule(const ccu_rep::ccu_instr_info& input)
{
    return schedule_cke_only(input);
}

// cke_only 默认档: 保持原序, 只对 CKE 寄存器的写后读 (某条 setcke 写 CKE, 之后 waitcke/
// clearcke 读同一 CKE) 按固定 cke latency 补 NOP; XN / MS 写后读交由硬件 interlock, 不补
// 任何 NOP. 每个 CKE 读者最多补 (L-1) 条 NOP, 与"每个 wait 类 rep 预留 L 条"精确对齐.
ccu_rep::ccu_instr_info instruction_scheduler::schedule_cke_only(const ccu_rep::ccu_instr_info& input)
{
    stats_ = {};
    const auto& orig_vec = input.instr_vec;
    const size_t instr_count = orig_vec.size();

    std::vector<ccu_rep::ccu_instr> out_vec;
    std::vector<int32_t> orig_to_out;
    out_vec.reserve(instr_count);
    orig_to_out.assign(instr_count, -1);

    // 预扫描识别 rel_jmp 原子块 (func-call / func-ret 运行期地址跳转), 块内禁止插 NOP.
    const std::vector<bool> rel_jmp_protected = mark_rel_jmp_protected_ranges(orig_vec);

    // 只跟踪 CKE 写者的发射 cycle; 不做启发式放大, 保证补 NOP 有界.
    // 索引用 size_t 与 vector::size() 对齐, 避免 instr_count 逼近 65535 时 uint16_t 回绕死循环;
    // 输出条数是否越界预留区由上游 trans_rep_sequence_to_microcode 按 instr_vec.size() 快速失败兜底.
    cke_only_state state{out_vec, orig_to_out, stats_, rel_jmp_protected};
    for (size_t i = 0; i < instr_count; ++i) {
        schedule_one_cke_instr(state, orig_vec[i], i);
    }

    // cke_only 不做 BB 切分, 用 1 作为占位 (仅统计意义).
    stats_.basic_blocks = instr_count > 0 ? 1 : 0;

    ccu_rep::ccu_instr_info out;
    out.instr_vec = std::move(out_vec);
    out.start_instr_id = input.start_instr_id;

    // instr_count 字段为 uint16_t. 正常路径下上游按 CKE 预留区申请, 优化后条数远小于 65535;
    // 但一旦补 NOP 后输出条数超过 uint16_t 上限, 直接截断会让 instr_count 与真实 instr_vec 大小
    // 不一致, 进而使 fix_references 的引用重映射错位. 此处显式记录错误再截断, 把静默数据损坏
    // 变成可观测告警; instr_vec 保留完整大小, 由上游 trans_rep_sequence_to_microcode 按
    // instr_vec.size() > region_size 快速失败兜底 (见 ccu_kernel_mgr.cc).
    constexpr size_t k_max_instr_count = std::numeric_limits<uint16_t>::max();
    if (out.instr_vec.size() > k_max_instr_count) {
        HCCL_ERROR(
            "[instruction_scheduler] optimized instr count[%zu] exceeds uint16_t range[%zu]; "
            "instr_count field will be truncated, upstream region-size check will reject it.",
            out.instr_vec.size(), k_max_instr_count);
    }
    out.instr_count = static_cast<uint16_t>(out.instr_vec.size());

    fix_references(input, orig_to_out, rel_jmp_protected, out);

    return out;
}

} // namespace ccu_opt
} // namespace asc
