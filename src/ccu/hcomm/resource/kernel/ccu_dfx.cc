/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/kernel/ccu_kernel_mgr.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>

#include "hcomm/resource/representation/ccu_rep_v1.h"

namespace asc {
namespace {

using ccu_rep::ccu_rep_base;
using ccu_rep::ccu_rep_type;

// 循环参数放在 XN 变量里（拆成循环次数、地址步长、循环上下文编号）
struct loop_xm {
    uint64_t loop_cnt : 13;
    uint64_t gsa_stride : 32;
    uint64_t loop_ctx_id : 8;
    uint64_t reserved : 11;
};

// 循环组参数放在 XN 变量里（拆成组内循环数、展开偏移、展开次数）
union loop_group_xn {
    uint64_t value;
    struct {
        uint64_t reserved_low : 41;
        uint64_t loop_ins_cnt : 7;
        uint64_t expand_offset : 7;
        uint64_t expand_cnt : 7;
        uint64_t reserved_high : 2;
    } fields;
};

// 诊断记录生成器：拿到故障请求（设备/die/任务/内核）后，通过 hcomm 提供的查询接口
// 读出当时的运行现场，把故障点附近的每条 REP 变成一条诊断记录，
// 再一条条通过 emit 回调发出去；数据面自己不直接碰驱动。
class dfx_builder {
public:
    dfx_builder(
        const HcommCcuDfxRequestPod& request, HcommCcuDfxEmitFn emit, void* context, const HcommCcuControlOpsPod& ops)
        : request_(request), emit_(emit), context_(context), ops_(ops)
    {}

    // 诊断主流程：查任务现场 → 找到内核 → 先发一条 MISSION 记录，
    // 再展开当前指令（是循环组 LOOPGROUP 就整组展开），最后把故障前 10 条
    // 指令的 REP 也逐条发出去；返回第一个错误码（没出错返回 0，找不到 REP 返回 CCU_E_NOT_FOUND）
    int32_t run()
    {
        HcommCcuMissionContextPod mission{};
        if (ops_.missionContextQuery == nullptr ||
            ops_.missionContextQuery(request_.deviceId, request_.dieId, request_.execMissionId, &mission) != 0) {
            return static_cast<int32_t>(CcuResult::CCU_E_INTERNAL);
        }
        ccu_kernel* kernel = ccu_kernel_mgr::get_instance(request_.deviceId).get_kernel(request_.kernelHandle);
        if (kernel == nullptr) {
            return static_cast<int32_t>(CcuResult::CCU_E_NOT_FOUND);
        }
        emit_mission(mission.currentInstructionId);
        auto current = resolve_rep(*kernel, mission.currentInstructionId);
        if (current == nullptr) {
            return static_cast<int32_t>(CcuResult::CCU_E_NOT_FOUND);
        }
        auto previous =
            mission.currentInstructionId == 0 ? nullptr : kernel->get_rep_by_instr_id(mission.currentInstructionId - 1);
        if ((previous != nullptr && previous->type() == ccu_rep_type::loop_group) ||
            current->type() == ccu_rep_type::loop_group) {
            emit_loop_group(
                *kernel, previous != nullptr && previous->type() == ccu_rep_type::loop_group ? previous : current);
        } else {
            emit_rep(current);
        }

        // 只回看故障前的 10 条指令，且不能越过本条任务开始的位置
        const uint16_t distance = 10;
        uint16_t begin = mission.currentInstructionId > distance ? mission.currentInstructionId - distance : 0;
        begin = std::max(begin, mission.startInstructionId);
        for (int32_t id = mission.currentInstructionId; id >= begin; --id) {
            if (kernel->get_rep_by_instr_id(static_cast<uint16_t>(id)) == nullptr) {
                // 这个编号没有对应的 REP（指令中间有空档），窗口就缩短到这里
                begin = static_cast<uint16_t>(id + 1);
                break;
            }
        }
        for (uint32_t id = begin; id <= mission.currentInstructionId; ++id) {
            auto rep = kernel->get_rep_by_instr_id(static_cast<uint16_t>(id));
            if (rep != nullptr) {
                emit_rep(rep);
            }
        }
        return first_error_;
    }

private:
    // 填记录里公共的部分：记录类型、REP 类型、die/任务编号、指令编号（REP 第一条指令所在位置）
    HcommCcuDfxRecordPod make_base_record(const std::shared_ptr<ccu_rep_base>& rep, uint32_t type) const
    {
        HcommCcuDfxRecordPod record{};
        record.type = type;
        record.repType = static_cast<int32_t>(rep->type());
        record.dieId = static_cast<uint8_t>(request_.dieId);
        record.missionId = static_cast<uint8_t>(request_.missionId);
        record.instrId = rep->start_instr_id();
        return record;
    }

    // 发一条记录；只记第一次失败，保证错误码稳定
    void emit(HcommCcuDfxRecordPod& record)
    {
        if (first_error_ == 0) {
            first_error_ = emit_(context_, &record);
        }
    }

    // 按编号查 XN/CKE/GSA 资源当时的数值；查不到返回 UINT64_MAX，并把错误记为内部错误
    uint64_t resource(HcommCcuResourceQueryFn query, uint32_t id)
    {
        uint64_t value = UINT64_MAX;
        if (query == nullptr || query(request_.deviceId, request_.dieId, id, &value) != 0) {
            first_error_ = first_error_ == 0 ? static_cast<int32_t>(CcuResult::CCU_E_INTERNAL) : first_error_;
        }
        return value;
    }

    // 单阶段查询 Channel POD 快照：先填充 ABI 头，再调用控制面注入的 channelQuery
    bool query_channel(ChannelHandle handle, HcommCcuChannelPod& channel)
    {
        channel.header.version = HCOMM_CCU_CHANNEL_ABI_VERSION;
        channel.header.magicWord = HCOMM_CCU_CHANNEL_POD_MAGIC_WORD;
        channel.header.size = sizeof(HcommCcuChannelPod);
        if (ops_.channelQuery == nullptr || ops_.channelQuery(handle, &channel) != 0) {
            first_error_ = first_error_ == 0 ? static_cast<int32_t>(CcuResult::CCU_E_INTERNAL) : first_error_;
            return false;
        }
        return true;
    }

    // 把通道句柄转成记录里要填的通道编号；解析失败就保持 UINT16_MAX（表示无效通道）
    void set_channel(ChannelHandle handle, uint16_t& id, ChannelHandle& record_handle)
    {
        HcommCcuChannelPod channel{};
        record_handle = handle;
        id = UINT16_MAX;
        // 查询失败时保留哨兵值 id=UINT16_MAX，交由上层按无效 channel 处理
        if (!query_channel(handle, channel)) {
            return;
        }
        // channelId 收窄为 uint16_t 前先做范围校验，避免静默截断
        if (channel.channelId > std::numeric_limits<uint16_t>::max()) {
            first_error_ = first_error_ == 0 ? static_cast<int32_t>(CcuResult::CCU_E_PARA) : first_error_;
            return;
        }
        id = static_cast<uint16_t>(channel.channelId);
    }

    // 从 POD 定长数组取第 index 个资源 id，收窄为 uint16_t 前做指针/越界/溢出校验
    uint16_t id_by_array(const uint32_t* ids, uint16_t index)
    {
        if (ids == nullptr || index >= HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY ||
            ids[index] > std::numeric_limits<uint16_t>::max()) {
            first_error_ = first_error_ == 0 ? static_cast<int32_t>(CcuResult::CCU_E_PARA) : first_error_;
            return UINT16_MAX;
        }
        return static_cast<uint16_t>(ids[index]);
    }

    // 取本端/远端 CKE（信号量）id：remote 为 true 时读远端数组，否则读本端数组
    uint16_t channel_signal(ChannelHandle handle, uint16_t index, bool remote)
    {
        HcommCcuChannelPod channel{};
        if (!query_channel(handle, channel)) {
            return UINT16_MAX;
        }
        return id_by_array(remote ? channel.remoteCkeIds : channel.localCkeIds, index);
    }

    // 取远端 XN（变量槽）id，用于分析 RV 类远程 POST 的变量地址
    uint16_t channel_remote_xn(ChannelHandle handle, uint16_t index)
    {
        HcommCcuChannelPod channel{};
        if (!query_channel(handle, channel)) {
            return UINT16_MAX;
        }
        return id_by_array(channel.remoteXnIds, index);
    }

    // 按指令编号找到这条指令属于哪条 REP：先在整个内核里找，
    // 找到 FUNC_BLOCK（函数调用的壳，自己不做事）就进到壳里面继续
    // 按同一编号找，直到找到真正干活的 REP；找不到返回 nullptr
    std::shared_ptr<ccu_rep_base> resolve_rep(ccu_kernel& kernel, uint16_t id)
    {
        auto rep = kernel.get_rep_by_instr_id(id);
        while (rep != nullptr && rep->type() == ccu_rep_type::func_block) {
            // FUNC_BLOCK 只是函数调用的壳，真正干活的是壳里的 REP，继续往里找
            rep = std::static_pointer_cast<ccu_rep::ccu_rep_block>(rep)->get_rep_by_instr_id(id);
        }
        return rep;
    }

    // 发 MISSION 记录：说明这次诊断的任务和当前指令编号
    void emit_mission(uint16_t instruction_id)
    {
        HcommCcuDfxRecordPod record{};
        record.type = HCOMM_CCU_DFX_RECORD_MISSION;
        record.repType = static_cast<int32_t>(ccu_rep_type::base);
        record.dieId = static_cast<uint8_t>(request_.dieId);
        record.missionId = static_cast<uint8_t>(request_.missionId);
        record.instrId = instruction_id;
        emit(record);
    }

    // 发 WAIT_SIGNAL 记录：本端在等某个信号（信号编号/掩码；readValue 时再读出信号当前值）
    void emit_wait(const std::shared_ptr<ccu_rep_base>& base, uint16_t signal, uint16_t mask, bool read_value)
    {
        auto record = make_base_record(base, HCOMM_CCU_DFX_RECORD_WAIT_SIGNAL);
        record.msg.waitSignal.signalId = signal;
        record.msg.waitSignal.signalMask = mask;
        record.msg.waitSignal.signalValue = read_value ? static_cast<uint16_t>(resource(ops_.ckeQuery, signal)) : 0;
        // 用不到的通道位置先填无效值 UINT16_MAX，方便 hcomm 那边识别
        std::fill(std::begin(record.msg.waitSignal.channelId), std::end(record.msg.waitSignal.channelId), UINT16_MAX);
        emit(record);
    }

    template <typename rep_t>
    // 发 WAIT_SIGNAL 记录：远端发信号/等信号的现场——远端信号编号从通道快照里取，
    // readValue 决定要不要再读一次信号当前值
    void emit_remote_wait(const std::shared_ptr<ccu_rep_base>& base, const std::shared_ptr<rep_t>& rep, bool read_value)
    {
        auto record = make_base_record(base, HCOMM_CCU_DFX_RECORD_WAIT_SIGNAL);
        set_channel(rep->get_channel(), record.msg.waitSignal.channelId[0], record.msg.waitSignal.channelHandle[0]);
        // POST 用远端 CKE（信号挂在对方），WAIT 用本端 CKE
        record.msg.waitSignal.signalId = channel_signal(rep->get_channel(), rep->get_sem_index(), !read_value);
        record.msg.waitSignal.signalMask = rep->get_mask();
        record.msg.waitSignal.signalValue =
            read_value ? static_cast<uint16_t>(resource(ops_.ckeQuery, record.msg.waitSignal.signalId)) : 0;
        std::fill(
            std::begin(record.msg.waitSignal.channelId) + 1, std::end(record.msg.waitSignal.channelId), UINT16_MAX);
        emit(record);
    }

    // 发 WAIT_SIGNAL 记录：远程写变量（RV）的现场——远端信号 + 对方变量的编号和当前值
    void emit_remote_post_var(const std::shared_ptr<ccu_rep_base>& base)
    {
        auto rep = std::static_pointer_cast<ccu_rep::ccu_rep_rem_post_var>(base);
        auto record = make_base_record(base, HCOMM_CCU_DFX_RECORD_WAIT_SIGNAL);
        set_channel(rep->get_channel(), record.msg.waitSignal.channelId[0], record.msg.waitSignal.channelHandle[0]);
        std::fill(
            std::begin(record.msg.waitSignal.channelId) + 1, std::end(record.msg.waitSignal.channelId), UINT16_MAX);
        record.msg.waitSignal.signalId = channel_signal(rep->get_channel(), rep->get_sem_index(), true);
        record.msg.waitSignal.signalMask = rep->get_mask();
        // 变量编号存在对端通道的 XN 数组里，变量的当前值再从 XN 里查出来
        record.msg.waitSignal.paramId = channel_remote_xn(rep->get_channel(), rep->get_param_index());
        record.msg.waitSignal.paramValue = resource(ops_.xnQuery, rep->get_param().id());
        emit(record);
    }

    // 发 TRANSFER 记录：读/写内存的搬运现场（本端和对端的地址、token、长度、信号、通道）
    template <typename rep_t>
    void emit_transfer(const std::shared_ptr<ccu_rep_base>& base, const std::shared_ptr<rep_t>& rep)
    {
        auto record = make_base_record(base, HCOMM_CCU_DFX_RECORD_TRANSFER);
        // 地址从 GSA 查、token 和长度从 XN 查，把资源编号还原成真正的数值
        record.msg.transMem.locAddr = resource(ops_.gsaQuery, rep->get_loc_addr_id());
        record.msg.transMem.locToken = resource(ops_.xnQuery, rep->get_loc_token_id());
        record.msg.transMem.rmtAddr = resource(ops_.gsaQuery, rep->get_rem_addr_id());
        record.msg.transMem.rmtToken = resource(ops_.xnQuery, rep->get_rem_token_id());
        record.msg.transMem.len = resource(ops_.xnQuery, rep->get_len_id());
        record.msg.transMem.signalId = rep->get_sem_id();
        record.msg.transMem.signalMask = rep->get_mask();
        set_channel(rep->get_channel(), record.msg.transMem.channelId, record.msg.transMem.channelHandle);
        record.msg.transMem.dataType = rep->get_data_type();
        record.msg.transMem.opType = rep->get_op_type();
        emit(record);
    }

    // 发 TRANSFER 记录：本地复制/归约的现场（源和目的都在本端，不走通道）
    void emit_local_copy(const std::shared_ptr<ccu_rep_base>& base)
    {
        auto rep = std::static_pointer_cast<ccu_rep::ccu_rep_loc_cpy>(base);
        auto record = make_base_record(base, HCOMM_CCU_DFX_RECORD_TRANSFER);
        // 本地复制也复用 TRANSFER 记录的格式（所谓“对端”其实还是本端内存）
        record.msg.transMem.locAddr = resource(ops_.gsaQuery, rep->get_src_addr_id());
        record.msg.transMem.locToken = resource(ops_.xnQuery, rep->get_src_token_id());
        record.msg.transMem.rmtAddr = resource(ops_.gsaQuery, rep->get_dst_addr_id());
        record.msg.transMem.rmtToken = resource(ops_.xnQuery, rep->get_dst_token_id());
        record.msg.transMem.len = resource(ops_.xnQuery, rep->get_len_id());
        record.msg.transMem.signalId = rep->get_sem_id();
        record.msg.transMem.signalMask = rep->get_mask();
        record.msg.transMem.dataType = rep->get_data_type();
        record.msg.transMem.opType = rep->get_op_type();
        emit(record);
    }

    // 发 BUFFER_TRANSFER 记录：从内存读到缓冲区（源是地址，目的是缓冲区编号）
    void emit_buffer_read(const std::shared_ptr<ccu_rep_base>& base)
    {
        auto rep = std::static_pointer_cast<ccu_rep::ccu_rep_buf_read>(base);
        auto record = make_base_record(base, HCOMM_CCU_DFX_RECORD_BUFFER_TRANSFER);
        record.msg.bufTransMem.bufId = rep->get_dst_addr_id() & 0x7fffU; // 去掉 bit15（IO Die 标志位），只留缓冲区编号
        record.msg.bufTransMem.addr = resource(ops_.gsaQuery, rep->get_src_addr_id());
        record.msg.bufTransMem.token = resource(ops_.xnQuery, rep->get_src_token_id());
        record.msg.bufTransMem.len = resource(ops_.xnQuery, rep->get_len_id());
        record.msg.bufTransMem.signalId = rep->get_sem_id();
        record.msg.bufTransMem.signalMask = rep->get_mask();
        set_channel(rep->get_channel(), record.msg.bufTransMem.channelId, record.msg.bufTransMem.channelHandle);
        emit(record);
    }

    // 发 BUFFER_TRANSFER 记录：从缓冲区写到内存（源是缓冲区编号，目的是地址）
    void emit_buffer_write(const std::shared_ptr<ccu_rep_base>& base)
    {
        auto rep = std::static_pointer_cast<ccu_rep::ccu_rep_buf_write>(base);
        auto record = make_base_record(base, HCOMM_CCU_DFX_RECORD_BUFFER_TRANSFER);
        record.msg.bufTransMem.bufId = rep->get_src_id() & 0x7fffU; // 同上：去掉 bit15 标志位
        record.msg.bufTransMem.addr = resource(ops_.gsaQuery, rep->get_dst_addr_id());
        record.msg.bufTransMem.token = resource(ops_.xnQuery, rep->get_dst_token_id());
        record.msg.bufTransMem.len = resource(ops_.xnQuery, rep->get_len_id());
        record.msg.bufTransMem.signalId = rep->get_sem_id();
        record.msg.bufTransMem.signalMask = rep->get_mask();
        set_channel(rep->get_channel(), record.msg.bufTransMem.channelId, record.msg.bufTransMem.channelHandle);
        emit(record);
    }

    // 发 BUFFER_TRANSFER 记录：本地缓冲区读（不走通道，通道编号填无效值）
    void emit_buffer_local_read(const std::shared_ptr<ccu_rep_base>& base)
    {
        auto rep = std::static_pointer_cast<ccu_rep::ccu_rep_buf_loc_read>(base);
        auto record = make_base_record(base, HCOMM_CCU_DFX_RECORD_BUFFER_TRANSFER);
        record.msg.bufTransMem.bufId = rep->get_dst_id() & 0x7fffU; // 同上：去掉 bit15 标志位
        record.msg.bufTransMem.addr = resource(ops_.gsaQuery, rep->get_src_addr_id());
        record.msg.bufTransMem.token = resource(ops_.xnQuery, rep->get_src_token_id());
        record.msg.bufTransMem.len = resource(ops_.xnQuery, rep->get_len_id());
        record.msg.bufTransMem.signalId = rep->get_sem_id();
        record.msg.bufTransMem.signalMask = rep->get_mask();
        record.msg.bufTransMem.channelId = UINT16_MAX;
        emit(record);
    }

    // 发 BUFFER_TRANSFER 记录：本地缓冲区写（不走通道，通道编号填无效值）
    void emit_buffer_local_write(const std::shared_ptr<ccu_rep_base>& base)
    {
        auto rep = std::static_pointer_cast<ccu_rep::ccu_rep_buf_loc_write>(base);
        auto record = make_base_record(base, HCOMM_CCU_DFX_RECORD_BUFFER_TRANSFER);
        record.msg.bufTransMem.bufId = rep->get_src_addr_id() & 0x7fffU; // 同上：去掉 bit15 标志位
        record.msg.bufTransMem.addr = resource(ops_.gsaQuery, rep->get_dst_addr_id());
        record.msg.bufTransMem.token = resource(ops_.xnQuery, rep->get_dst_token_id());
        record.msg.bufTransMem.len = resource(ops_.xnQuery, rep->get_len_id());
        record.msg.bufTransMem.signalId = rep->get_sem_id();
        record.msg.bufTransMem.signalMask = rep->get_mask();
        record.msg.bufTransMem.channelId = UINT16_MAX;
        emit(record);
    }

    // 发 BUFFER_REDUCE 记录：多个缓冲区一起做归约（参加的缓冲区编号、个数、数据类型、算子类型等）
    void emit_buffer_reduce(const std::shared_ptr<ccu_rep_base>& base)
    {
        auto rep = std::static_pointer_cast<ccu_rep::ccu_rep_buf_reduce>(base);
        auto record = make_base_record(base, HCOMM_CCU_DFX_RECORD_BUFFER_REDUCE);
        // 数组先全填无效值，再放实际参与的缓冲区编号（同样去掉 bit15）
        std::fill(std::begin(record.msg.bufferReduce.bufIds), std::end(record.msg.bufferReduce.bufIds), UINT16_MAX);
        const auto& buffers = rep->get_mem();
        for (size_t i = 0; i < std::min(buffers.size(), static_cast<size_t>(HCOMM_CCU_DFX_BUFFER_CAPACITY)); ++i) {
            record.msg.bufferReduce.bufIds[i] = buffers[i].id() & 0x7fffU;
        }
        record.msg.bufferReduce.count = rep->get_count();
        record.msg.bufferReduce.dataType = rep->get_data_type();
        record.msg.bufferReduce.outputDataType = rep->get_output_data_type();
        record.msg.bufferReduce.opType = rep->get_op_type();
        record.msg.bufferReduce.signalId = rep->get_sem_id();
        record.msg.bufferReduce.signalMask = rep->get_mask();
        record.msg.bufferReduce.xnIdLength = rep->get_xn_length_id();
        emit(record);
    }

    // 发 LOOP 记录并展开循环体：先从 XN 拿循环参数（上下文编号、次数），
    // 再查循环上下文得到当前次数和地址步长，把循环体内每条指令的 REP 也逐条发出去
    void emit_loop(ccu_kernel& kernel, uint16_t instruction_id)
    {
        auto base = kernel.get_rep_by_instr_id(instruction_id);
        if (base == nullptr || base->type() != ccu_rep_type::loop) {
            return;
        }
        auto rep = std::static_pointer_cast<ccu_rep::ccu_rep_loop>(base);
        // 循环参数存在 XN 变量里（拆出次数、步长、循环上下文编号）
        union {
            uint64_t value;
            loop_xm fields;
        } loop{};
        loop.value = resource(ops_.xnQuery, rep->get_loop_param()->id());
        HcommCcuLoopContextPod context{};
        if (ops_.loopContextQuery == nullptr ||
            ops_.loopContextQuery(request_.deviceId, request_.dieId, loop.fields.loop_ctx_id, &context) != 0) {
            first_error_ = first_error_ == 0 ? static_cast<int32_t>(CcuResult::CCU_E_INTERNAL) : first_error_;
            return;
        }
        auto record = make_base_record(base, HCOMM_CCU_DFX_RECORD_LOOP);
        record.msg.loop.startInstrId = rep->get_loop_block()->start_instr_id();
        record.msg.loop.endInstrId = record.msg.loop.startInstrId + rep->get_loop_block()->instr_count() - 1;
        record.msg.loop.loopEngineId = loop.fields.loop_ctx_id;
        record.msg.loop.loopCnt = loop.fields.loop_cnt;
        record.msg.loop.loopCurrentCnt = context.currentCount;
        record.msg.loop.addrStride = context.addressStride;
        emit(record);
        // 循环体内的每条指令也逐个展开（在循环块里再按编号找对应的 REP）
        for (uint32_t id = record.msg.loop.startInstrId; id <= record.msg.loop.endInstrId; ++id) {
            emit_rep(rep->get_loop_block()->get_rep_by_instr_id(static_cast<uint16_t>(id)));
        }
    }

    // 发 LOOP_GROUP 记录并展开整组循环：从 XN 拿组内循环数、展开偏移、展开次数，逐个发 LOOP 记录
    void emit_loop_group(ccu_kernel& kernel, const std::shared_ptr<ccu_rep_base>& base)
    {
        if (base == nullptr) {
            return;
        }
        auto rep = std::static_pointer_cast<ccu_rep::ccu_rep_loop_group_bundle>(base);
        // 循环组参数也在 XN 变量里（组内循环数、展开偏移、展开次数）
        loop_group_xn group{};
        group.value = resource(ops_.xnQuery, rep->get_offset_param().id());
        auto record = make_base_record(base, HCOMM_CCU_DFX_RECORD_LOOP_GROUP);
        record.msg.loopGroup.startLoopInsId = rep->get_start_loop_instr_id();
        record.msg.loopGroup.loopInsCnt = group.fields.loop_ins_cnt;
        record.msg.loopGroup.expandOffset = group.fields.expand_offset;
        record.msg.loopGroup.expandCount = group.fields.expand_cnt;
        emit(record);
        for (uint32_t i = 0; i < group.fields.loop_ins_cnt; ++i) {
            emit_loop(kernel, static_cast<uint16_t>(record.msg.loopGroup.startLoopInsId + i));
        }
    }

    // 按 REP 的类型发出对应记录；LOC_WAIT_EVENT 有信号位还没被置上时，
    // 按依赖关系把在等的前置 REP 也补发出来；不认识的类型发 DEFAULT 记录兜底
    void emit_rep(const std::shared_ptr<ccu_rep_base>& base)
    {
        if (base == nullptr) {
            return;
        }
        switch (base->type()) {
            case ccu_rep_type::loc_record_event: {
                auto rep = std::static_pointer_cast<ccu_rep::ccu_rep_loc_record_event>(base);
                emit_wait(base, rep->get_event_id(), rep->get_mask(), true);
                break;
            }
            case ccu_rep_type::loc_wait_event: {
                auto rep = std::static_pointer_cast<ccu_rep::ccu_rep_loc_wait_event>(base);
                const uint16_t actual = static_cast<uint16_t>(resource(ops_.ckeQuery, rep->get_event_id()));
                emit_wait(base, rep->get_event_id(), rep->get_mask(), true);
                for (uint32_t bit = 1; bit != 0 && bit <= UINT16_MAX; bit <<= 1U) {
                    // 还有信号位没被置上，故障很可能就卡在等这些信号上，把它们依赖的 REP 也补发出来
                    if ((rep->get_mask() & bit) != 0 && (actual & bit) == 0) {
                        for (const auto& dependency : rep->get_dependency_info(bit)) {
                            emit_rep(dependency);
                        }
                    }
                }
                break;
            }
            case ccu_rep_type::loc_wait_notify: {
                auto rep = std::static_pointer_cast<ccu_rep::ccu_rep_loc_wait_notify>(base);
                emit_wait(base, rep->get_notify_id(), rep->get_mask(), true);
                break;
            }
            case ccu_rep_type::rem_post_sem:
                emit_remote_wait(base, std::static_pointer_cast<ccu_rep::ccu_rep_rem_post_sem>(base), false);
                break;
            case ccu_rep_type::rem_wait_sem:
                emit_remote_wait(base, std::static_pointer_cast<ccu_rep::ccu_rep_rem_wait_sem>(base), true);
                break;
            case ccu_rep_type::rem_post_var:
                emit_remote_post_var(base);
                break;
            case ccu_rep_type::record_shared_notify: {
                auto rep = std::static_pointer_cast<ccu_rep::ccu_rep_record_shared_notify>(base);
                emit_wait(base, rep->get_notify_id(), rep->get_mask(), false);
                break;
            }
            case ccu_rep_type::read:
                emit_transfer(base, std::static_pointer_cast<ccu_rep::ccu_rep_read>(base));
                break;
            case ccu_rep_type::write:
                emit_transfer(base, std::static_pointer_cast<ccu_rep::ccu_rep_write>(base));
                break;
            case ccu_rep_type::local_cpy:
            case ccu_rep_type::local_reduce:
                emit_local_copy(base);
                break;
            case ccu_rep_type::buf_read:
                emit_buffer_read(base);
                break;
            case ccu_rep_type::buf_write:
                emit_buffer_write(base);
                break;
            case ccu_rep_type::buf_loc_read:
                emit_buffer_local_read(base);
                break;
            case ccu_rep_type::buf_loc_write:
                emit_buffer_local_write(base);
                break;
            case ccu_rep_type::buf_reduce:
                emit_buffer_reduce(base);
                break;
            default: {
                auto record = make_base_record(base, HCOMM_CCU_DFX_RECORD_DEFAULT);
                emit(record);
                break;
            }
        }
    }

    const HcommCcuDfxRequestPod& request_;
    HcommCcuDfxEmitFn emit_;
    void* context_;
    const HcommCcuControlOpsPod& ops_;
    int32_t first_error_{0};
};

} // namespace

// 诊断入口（hcomm 控制面在任务异常时跨 SO 调过来）：先校验参数，
// 再取本设备的内核管理器和 hcomm 注入的查询接口，交给 DfxBuilder 生成全部记录，
// 逐条经 emit 回调发出；出错一律返回内部错误，参数不对返回参数错误
int32_t asccomm_ccu_diagnose(const HcommCcuDfxRequestPod* request, HcommCcuDfxEmitFn emit, void* context)
{
    if (request == nullptr || emit == nullptr || request->deviceId < 0 || request->kernelHandle == 0) {
        return static_cast<int32_t>(CcuResult::CCU_E_PARA);
    }
    try {
        ccu_kernel_mgr& manager = ccu_kernel_mgr::get_instance(request->deviceId);
        return dfx_builder(*request, emit, context, manager.get_control_ops()).run();
    } catch (...) {
        return static_cast<int32_t>(CcuResult::CCU_E_INTERNAL);
    }
}

} // namespace asc
