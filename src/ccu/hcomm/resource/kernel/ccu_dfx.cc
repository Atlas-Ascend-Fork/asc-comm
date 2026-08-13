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

namespace asccomm {
namespace {

using CcuRep::CcuRepBase;
using CcuRep::CcuRepType;

// 循环参数放在 XN 变量里（拆成循环次数、地址步长、循环上下文编号）
struct LoopXm {
    uint64_t loopCnt : 13;
    uint64_t gsaStride : 32;
    uint64_t loopCtxId : 8;
    uint64_t reserved : 11;
};

// 循环组参数放在 XN 变量里（拆成组内循环数、展开偏移、展开次数）
union LoopGroupXn {
    uint64_t value;
    struct {
        uint64_t reservedLow : 41;
        uint64_t loopInsCnt : 7;
        uint64_t expandOffset : 7;
        uint64_t expandCnt : 7;
        uint64_t reservedHigh : 2;
    } fields;
};

// 诊断记录生成器：拿到故障请求（设备/die/任务/内核）后，通过 hcomm 提供的查询接口
// 读出当时的运行现场，把故障点附近的每条 REP 变成一条诊断记录，
// 再一条条通过 emit 回调发出去；数据面自己不直接碰驱动。
class DfxBuilder {
public:
    DfxBuilder(const HcommCcuDfxRequestPod &request, HcommCcuDfxEmitFn emit, void *context,
        const HcommCcuControlOpsPod &ops)
        : request_(request), emit_(emit), context_(context), ops_(ops)
    {
    }

    // 诊断主流程：查任务现场 → 找到内核 → 先发一条 MISSION 记录，
    // 再展开当前指令（是循环组 LOOPGROUP 就整组展开），最后把故障前 10 条
    // 指令的 REP 也逐条发出去；返回第一个错误码（没出错返回 0，找不到 REP 返回 CCU_E_NOT_FOUND）
    int32_t Run()
    {
        HcommCcuMissionContextPod mission{};
        if (ops_.missionContextQuery == nullptr ||
            ops_.missionContextQuery(request_.deviceId, request_.dieId, request_.execMissionId, &mission) != 0) {
            return static_cast<int32_t>(CcuResult::CCU_E_INTERNAL);
        }
        CcuKernel *kernel = CcuKernelMgr::GetInstance(request_.deviceId).GetKernel(request_.kernelHandle);
        if (kernel == nullptr) {
            return static_cast<int32_t>(CcuResult::CCU_E_NOT_FOUND);
        }
        EmitMission(mission.currentInstructionId);
        auto current = ResolveRep(*kernel, mission.currentInstructionId);
        if (current == nullptr) {
            return static_cast<int32_t>(CcuResult::CCU_E_NOT_FOUND);
        }
        auto previous = mission.currentInstructionId == 0 ? nullptr : kernel->GetRepByInstrId(mission.currentInstructionId - 1);
        if ((previous != nullptr && previous->Type() == CcuRepType::LOOPGROUP) ||
            current->Type() == CcuRepType::LOOPGROUP) {
            EmitLoopGroup(*kernel, previous != nullptr && previous->Type() == CcuRepType::LOOPGROUP ? previous : current);
        } else {
            EmitRep(current);
        }

        // 只回看故障前的 10 条指令，且不能越过本条任务开始的位置
        const uint16_t distance = 10;
        uint16_t begin = mission.currentInstructionId > distance ? mission.currentInstructionId - distance : 0;
        begin = std::max(begin, mission.startInstructionId);
        for (int32_t id = mission.currentInstructionId; id >= begin; --id) {
            if (kernel->GetRepByInstrId(static_cast<uint16_t>(id)) == nullptr) {
                // 这个编号没有对应的 REP（指令中间有空档），窗口就缩短到这里
                begin = static_cast<uint16_t>(id + 1);
                break;
            }
        }
        for (uint32_t id = begin; id <= mission.currentInstructionId; ++id) {
            auto rep = kernel->GetRepByInstrId(static_cast<uint16_t>(id));
            if (rep != nullptr) {
                EmitRep(rep);
            }
        }
        return firstError_;
    }

private:
    // 填记录里公共的部分：记录类型、REP 类型、die/任务编号、指令编号（REP 第一条指令所在位置）
    HcommCcuDfxRecordPod Base(const std::shared_ptr<CcuRepBase> &rep, uint32_t type) const
    {
        HcommCcuDfxRecordPod record{};
        record.type = type;
        record.repType = static_cast<int32_t>(rep->Type());
        record.dieId = static_cast<uint8_t>(request_.dieId);
        record.missionId = static_cast<uint8_t>(request_.missionId);
        record.instrId = rep->StartInstrId();
        return record;
    }

    // 发一条记录；只记第一次失败，保证错误码稳定
    void Emit(HcommCcuDfxRecordPod &record)
    {
        if (firstError_ == 0) {
            firstError_ = emit_(context_, &record);
        }
    }

    // 按编号查 XN/CKE/GSA 资源当时的数值；查不到返回 UINT64_MAX，并把错误记为内部错误
    uint64_t Resource(HcommCcuResourceQueryFn query, uint32_t id)
    {
        uint64_t value = UINT64_MAX;
        if (query == nullptr || query(request_.deviceId, request_.dieId, id, &value) != 0) {
            firstError_ = firstError_ == 0 ? static_cast<int32_t>(CcuResult::CCU_E_INTERNAL) : firstError_;
        }
        return value;
    }

    // 单阶段查询 Channel POD 快照：先填充 ABI 头，再调用控制面注入的 channelQuery
    bool QueryChannel(ChannelHandle handle, HcommCcuChannelPod &channel)
    {
        channel.header.version = HCOMM_CCU_CHANNEL_ABI_VERSION;
        channel.header.magicWord = HCOMM_CCU_CHANNEL_POD_MAGIC_WORD;
        channel.header.size = sizeof(HcommCcuChannelPod);
        if (ops_.channelQuery == nullptr || ops_.channelQuery(handle, &channel) != 0) {
            firstError_ = firstError_ == 0 ? static_cast<int32_t>(CcuResult::CCU_E_INTERNAL) : firstError_;
            return false;
        }
        return true;
    }

    // 把通道句柄转成记录里要填的通道编号；解析失败就保持 UINT16_MAX（表示无效通道）
    void SetChannel(ChannelHandle handle, uint16_t &id, ChannelHandle &recordHandle)
    {
        HcommCcuChannelPod channel{};
        recordHandle = handle;
        id = UINT16_MAX;
        // 查询失败时保留哨兵值 id=UINT16_MAX，交由上层按无效 channel 处理
        if (!QueryChannel(handle, channel)) {
            return;
        }
        // channelId 收窄为 uint16_t 前先做范围校验，避免静默截断
        if (channel.channelId > std::numeric_limits<uint16_t>::max()) {
            firstError_ = firstError_ == 0 ? static_cast<int32_t>(CcuResult::CCU_E_PARA) : firstError_;
            return;
        }
        id = static_cast<uint16_t>(channel.channelId);
    }

    // 从 POD 定长数组取第 index 个资源 id，收窄为 uint16_t 前做指针/越界/溢出校验
    uint16_t IdByArray(const uint32_t *ids, uint16_t index)
    {
        if (ids == nullptr || index >= HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY ||
            ids[index] > std::numeric_limits<uint16_t>::max()) {
            firstError_ = firstError_ == 0 ? static_cast<int32_t>(CcuResult::CCU_E_PARA) : firstError_;
            return UINT16_MAX;
        }
        return static_cast<uint16_t>(ids[index]);
    }

    // 取本端/远端 CKE（信号量）id：remote 为 true 时读远端数组，否则读本端数组
    uint16_t ChannelSignal(ChannelHandle handle, uint16_t index, bool remote)
    {
        HcommCcuChannelPod channel{};
        if (!QueryChannel(handle, channel)) {
            return UINT16_MAX;
        }
        return IdByArray(remote ? channel.remoteCkeIds : channel.localCkeIds, index);
    }

    // 取远端 XN（变量槽）id，用于分析 RV 类远程 POST 的变量地址
    uint16_t ChannelRemoteXn(ChannelHandle handle, uint16_t index)
    {
        HcommCcuChannelPod channel{};
        if (!QueryChannel(handle, channel)) {
            return UINT16_MAX;
        }
        return IdByArray(channel.remoteXnIds, index);
    }

    // 按指令编号找到这条指令属于哪条 REP：先在整个内核里找，
    // 找到 FUNC_BLOCK（函数调用的壳，自己不做事）就进到壳里面继续
    // 按同一编号找，直到找到真正干活的 REP；找不到返回 nullptr
    std::shared_ptr<CcuRepBase> ResolveRep(CcuKernel &kernel, uint16_t id)
    {
        auto rep = kernel.GetRepByInstrId(id);
        while (rep != nullptr && rep->Type() == CcuRepType::FUNC_BLOCK) {
            // FUNC_BLOCK 只是函数调用的壳，真正干活的是壳里的 REP，继续往里找
            rep = std::static_pointer_cast<CcuRep::CcuRepBlock>(rep)->GetRepByInstrId(id);
        }
        return rep;
    }

    // 发 MISSION 记录：说明这次诊断的任务和当前指令编号
    void EmitMission(uint16_t instructionId)
    {
        HcommCcuDfxRecordPod record{};
        record.type = HCOMM_CCU_DFX_RECORD_MISSION;
        record.repType = static_cast<int32_t>(CcuRepType::BASE);
        record.dieId = static_cast<uint8_t>(request_.dieId);
        record.missionId = static_cast<uint8_t>(request_.missionId);
        record.instrId = instructionId;
        Emit(record);
    }

    // 发 WAIT_SIGNAL 记录：本端在等某个信号（信号编号/掩码；readValue 时再读出信号当前值）
    void EmitWait(const std::shared_ptr<CcuRepBase> &base, uint16_t signal, uint16_t mask, bool readValue)
    {
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_WAIT_SIGNAL);
        record.msg.waitSignal.signalId = signal;
        record.msg.waitSignal.signalMask = mask;
        record.msg.waitSignal.signalValue = readValue ? static_cast<uint16_t>(Resource(ops_.ckeQuery, signal)) : 0;
        // 用不到的通道位置先填无效值 UINT16_MAX，方便 hcomm 那边识别
        std::fill(std::begin(record.msg.waitSignal.channelId), std::end(record.msg.waitSignal.channelId), UINT16_MAX);
        Emit(record);
    }

    template <typename Rep>
    // 发 WAIT_SIGNAL 记录：远端发信号/等信号的现场——远端信号编号从通道快照里取，
    // readValue 决定要不要再读一次信号当前值
    void EmitRemoteWait(const std::shared_ptr<CcuRepBase> &base, const std::shared_ptr<Rep> &rep, bool readValue)
    {
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_WAIT_SIGNAL);
        SetChannel(rep->GetChannel(), record.msg.waitSignal.channelId[0], record.msg.waitSignal.channelHandle[0]);
        // POST 用远端 CKE（信号挂在对方），WAIT 用本端 CKE
        record.msg.waitSignal.signalId = ChannelSignal(rep->GetChannel(), rep->GetSemIndex(), !readValue);
        record.msg.waitSignal.signalMask = rep->GetMask();
        record.msg.waitSignal.signalValue = readValue ?
            static_cast<uint16_t>(Resource(ops_.ckeQuery, record.msg.waitSignal.signalId)) : 0;
        std::fill(std::begin(record.msg.waitSignal.channelId) + 1, std::end(record.msg.waitSignal.channelId), UINT16_MAX);
        Emit(record);
    }

    // 发 WAIT_SIGNAL 记录：远程写变量（RV）的现场——远端信号 + 对方变量的编号和当前值
    void EmitRemotePostVar(const std::shared_ptr<CcuRepBase> &base)
    {
        auto rep = std::static_pointer_cast<CcuRep::CcuRepRemPostVar>(base);
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_WAIT_SIGNAL);
        SetChannel(rep->GetChannel(), record.msg.waitSignal.channelId[0], record.msg.waitSignal.channelHandle[0]);
        std::fill(std::begin(record.msg.waitSignal.channelId) + 1, std::end(record.msg.waitSignal.channelId), UINT16_MAX);
        record.msg.waitSignal.signalId = ChannelSignal(rep->GetChannel(), rep->GetSemIndex(), true);
        record.msg.waitSignal.signalMask = rep->GetMask();
        // 变量编号存在对端通道的 XN 数组里，变量的当前值再从 XN 里查出来
        record.msg.waitSignal.paramId = ChannelRemoteXn(rep->GetChannel(), rep->GetParamIndex());
        record.msg.waitSignal.paramValue = Resource(ops_.xnQuery, rep->GetParam().Id());
        Emit(record);
    }

    // 发 TRANSFER 记录：读/写内存的搬运现场（本端和对端的地址、token、长度、信号、通道）
    template <typename Rep>
    void EmitTransfer(const std::shared_ptr<CcuRepBase> &base, const std::shared_ptr<Rep> &rep)
    {
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_TRANSFER);
        // 地址从 GSA 查、token 和长度从 XN 查，把资源编号还原成真正的数值
        record.msg.transMem.locAddr = Resource(ops_.gsaQuery, rep->GetLocAddrId());
        record.msg.transMem.locToken = Resource(ops_.xnQuery, rep->GetLocTokenId());
        record.msg.transMem.rmtAddr = Resource(ops_.gsaQuery, rep->GetRemAddrId());
        record.msg.transMem.rmtToken = Resource(ops_.xnQuery, rep->GetRemTokenId());
        record.msg.transMem.len = Resource(ops_.xnQuery, rep->GetLenId());
        record.msg.transMem.signalId = rep->GetSemId();
        record.msg.transMem.signalMask = rep->GetMask();
        SetChannel(rep->GetChannel(), record.msg.transMem.channelId, record.msg.transMem.channelHandle);
        record.msg.transMem.dataType = rep->GetDataType();
        record.msg.transMem.opType = rep->GetOpType();
        Emit(record);
    }

    // 发 TRANSFER 记录：本地复制/归约的现场（源和目的都在本端，不走通道）
    void EmitLocalCopy(const std::shared_ptr<CcuRepBase> &base)
    {
        auto rep = std::static_pointer_cast<CcuRep::CcuRepLocCpy>(base);
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_TRANSFER);
        // 本地复制也复用 TRANSFER 记录的格式（所谓“对端”其实还是本端内存）
        record.msg.transMem.locAddr = Resource(ops_.gsaQuery, rep->GetSrcAddrId());
        record.msg.transMem.locToken = Resource(ops_.xnQuery, rep->GetSrcTokenId());
        record.msg.transMem.rmtAddr = Resource(ops_.gsaQuery, rep->GetDstAddrId());
        record.msg.transMem.rmtToken = Resource(ops_.xnQuery, rep->GetDstTokenId());
        record.msg.transMem.len = Resource(ops_.xnQuery, rep->GetLenId());
        record.msg.transMem.signalId = rep->GetSemId();
        record.msg.transMem.signalMask = rep->GetMask();
        record.msg.transMem.dataType = rep->GetDataType();
        record.msg.transMem.opType = rep->GetOpType();
        Emit(record);
    }

    // 发 BUFFER_TRANSFER 记录：从内存读到缓冲区（源是地址，目的是缓冲区编号）
    void EmitBufferRead(const std::shared_ptr<CcuRepBase> &base)
    {
        auto rep = std::static_pointer_cast<CcuRep::CcuRepBufRead>(base);
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_BUFFER_TRANSFER);
        record.msg.bufTransMem.bufId = rep->GetDstAddrId() & 0x7fffU; // 去掉 bit15（IO Die 标志位），只留缓冲区编号
        record.msg.bufTransMem.addr = Resource(ops_.gsaQuery, rep->GetSrcAddrId());
        record.msg.bufTransMem.token = Resource(ops_.xnQuery, rep->GetSrcTokenId());
        record.msg.bufTransMem.len = Resource(ops_.xnQuery, rep->GetLenId());
        record.msg.bufTransMem.signalId = rep->GetSemId();
        record.msg.bufTransMem.signalMask = rep->GetMask();
        SetChannel(rep->GetChannel(), record.msg.bufTransMem.channelId, record.msg.bufTransMem.channelHandle);
        Emit(record);
    }

    // 发 BUFFER_TRANSFER 记录：从缓冲区写到内存（源是缓冲区编号，目的是地址）
    void EmitBufferWrite(const std::shared_ptr<CcuRepBase> &base)
    {
        auto rep = std::static_pointer_cast<CcuRep::CcuRepBufWrite>(base);
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_BUFFER_TRANSFER);
        record.msg.bufTransMem.bufId = rep->GetSrcId() & 0x7fffU;    // 同上：去掉 bit15 标志位
        record.msg.bufTransMem.addr = Resource(ops_.gsaQuery, rep->GetDstAddrId());
        record.msg.bufTransMem.token = Resource(ops_.xnQuery, rep->GetDstTokenId());
        record.msg.bufTransMem.len = Resource(ops_.xnQuery, rep->GetLenId());
        record.msg.bufTransMem.signalId = rep->GetSemId();
        record.msg.bufTransMem.signalMask = rep->GetMask();
        SetChannel(rep->GetChannel(), record.msg.bufTransMem.channelId, record.msg.bufTransMem.channelHandle);
        Emit(record);
    }

    // 发 BUFFER_TRANSFER 记录：本地缓冲区读（不走通道，通道编号填无效值）
    void EmitBufferLocalRead(const std::shared_ptr<CcuRepBase> &base)
    {
        auto rep = std::static_pointer_cast<CcuRep::CcuRepBufLocRead>(base);
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_BUFFER_TRANSFER);
        record.msg.bufTransMem.bufId = rep->GetDstId() & 0x7fffU;   // 同上：去掉 bit15 标志位
        record.msg.bufTransMem.addr = Resource(ops_.gsaQuery, rep->GetSrcAddrId());
        record.msg.bufTransMem.token = Resource(ops_.xnQuery, rep->GetSrcTokenId());
        record.msg.bufTransMem.len = Resource(ops_.xnQuery, rep->GetLenId());
        record.msg.bufTransMem.signalId = rep->GetSemId();
        record.msg.bufTransMem.signalMask = rep->GetMask();
        record.msg.bufTransMem.channelId = UINT16_MAX;
        Emit(record);
    }

    // 发 BUFFER_TRANSFER 记录：本地缓冲区写（不走通道，通道编号填无效值）
    void EmitBufferLocalWrite(const std::shared_ptr<CcuRepBase> &base)
    {
        auto rep = std::static_pointer_cast<CcuRep::CcuRepBufLocWrite>(base);
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_BUFFER_TRANSFER);
        record.msg.bufTransMem.bufId = rep->GetSrcAddrId() & 0x7fffU; // 同上：去掉 bit15 标志位
        record.msg.bufTransMem.addr = Resource(ops_.gsaQuery, rep->GetDstAddrId());
        record.msg.bufTransMem.token = Resource(ops_.xnQuery, rep->GetDstTokenId());
        record.msg.bufTransMem.len = Resource(ops_.xnQuery, rep->GetLenId());
        record.msg.bufTransMem.signalId = rep->GetSemId();
        record.msg.bufTransMem.signalMask = rep->GetMask();
        record.msg.bufTransMem.channelId = UINT16_MAX;
        Emit(record);
    }

    // 发 BUFFER_REDUCE 记录：多个缓冲区一起做归约（参加的缓冲区编号、个数、数据类型、算子类型等）
    void EmitBufferReduce(const std::shared_ptr<CcuRepBase> &base)
    {
        auto rep = std::static_pointer_cast<CcuRep::CcuRepBufReduce>(base);
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_BUFFER_REDUCE);
        // 数组先全填无效值，再放实际参与的缓冲区编号（同样去掉 bit15）
        std::fill(std::begin(record.msg.bufferReduce.bufIds), std::end(record.msg.bufferReduce.bufIds), UINT16_MAX);
        const auto &buffers = rep->GetMem();
        for (size_t i = 0; i < std::min(buffers.size(), static_cast<size_t>(HCOMM_CCU_DFX_BUFFER_CAPACITY)); ++i) {
            record.msg.bufferReduce.bufIds[i] = buffers[i].Id() & 0x7fffU;
        }
        record.msg.bufferReduce.count = rep->GetCount();
        record.msg.bufferReduce.dataType = rep->GetDataType();
        record.msg.bufferReduce.outputDataType = rep->GetOutputDataType();
        record.msg.bufferReduce.opType = rep->GetOpType();
        record.msg.bufferReduce.signalId = rep->GetSemId();
        record.msg.bufferReduce.signalMask = rep->GetMask();
        record.msg.bufferReduce.xnIdLength = rep->GetXnLengthId();
        Emit(record);
    }

    // 发 LOOP 记录并展开循环体：先从 XN 拿循环参数（上下文编号、次数），
    // 再查循环上下文得到当前次数和地址步长，把循环体内每条指令的 REP 也逐条发出去
    void EmitLoop(CcuKernel &kernel, uint16_t instructionId)
    {
        auto base = kernel.GetRepByInstrId(instructionId);
        if (base == nullptr || base->Type() != CcuRepType::LOOP) {
            return;
        }
        auto rep = std::static_pointer_cast<CcuRep::CcuRepLoop>(base);
        // 循环参数存在 XN 变量里（拆出次数、步长、循环上下文编号）
        union { uint64_t value; LoopXm fields; } loop{};
        loop.value = Resource(ops_.xnQuery, rep->GetLoopParam()->Id());
        HcommCcuLoopContextPod context{};
        if (ops_.loopContextQuery == nullptr ||
            ops_.loopContextQuery(request_.deviceId, request_.dieId, loop.fields.loopCtxId, &context) != 0) {
            firstError_ = firstError_ == 0 ? static_cast<int32_t>(CcuResult::CCU_E_INTERNAL) : firstError_;
            return;
        }
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_LOOP);
        record.msg.loop.startInstrId = rep->GetLoopBlock()->StartInstrId();
        record.msg.loop.endInstrId = record.msg.loop.startInstrId + rep->GetLoopBlock()->InstrCount() - 1;
        record.msg.loop.loopEngineId = loop.fields.loopCtxId;
        record.msg.loop.loopCnt = loop.fields.loopCnt;
        record.msg.loop.loopCurrentCnt = context.currentCount;
        record.msg.loop.addrStride = context.addressStride;
        Emit(record);
        // 循环体内的每条指令也逐个展开（在循环块里再按编号找对应的 REP）
        for (uint32_t id = record.msg.loop.startInstrId; id <= record.msg.loop.endInstrId; ++id) {
            EmitRep(rep->GetLoopBlock()->GetRepByInstrId(static_cast<uint16_t>(id)));
        }
    }

    // 发 LOOP_GROUP 记录并展开整组循环：从 XN 拿组内循环数、展开偏移、展开次数，逐个发 LOOP 记录
    void EmitLoopGroup(CcuKernel &kernel, const std::shared_ptr<CcuRepBase> &base)
    {
        if (base == nullptr) {
            return;
        }
        auto rep = std::static_pointer_cast<CcuRep::CcuRepLoopGroupBundle>(base);
        // 循环组参数也在 XN 变量里（组内循环数、展开偏移、展开次数）
        LoopGroupXn group{};
        group.value = Resource(ops_.xnQuery, rep->GetOffsetParam().Id());
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_LOOP_GROUP);
        record.msg.loopGroup.startLoopInsId = rep->GetStartLoopInstrId();
        record.msg.loopGroup.loopInsCnt = group.fields.loopInsCnt;
        record.msg.loopGroup.expandOffset = group.fields.expandOffset;
        record.msg.loopGroup.expandCount = group.fields.expandCnt;
        Emit(record);
        for (uint32_t i = 0; i < group.fields.loopInsCnt; ++i) {
            EmitLoop(kernel, static_cast<uint16_t>(record.msg.loopGroup.startLoopInsId + i));
        }
    }

    // 按 REP 的类型发出对应记录；LOC_WAIT_EVENT 有信号位还没被置上时，
    // 按依赖关系把在等的前置 REP 也补发出来；不认识的类型发 DEFAULT 记录兜底
    void EmitRep(const std::shared_ptr<CcuRepBase> &base)
    {
        if (base == nullptr) {
            return;
        }
        switch (base->Type()) {
            case CcuRepType::LOC_RECORD_EVENT: {
                auto rep = std::static_pointer_cast<CcuRep::CcuRepLocRecordEvent>(base);
                EmitWait(base, rep->GetEventId(), rep->GetMask(), true); break;
            }
            case CcuRepType::LOC_WAIT_EVENT: {
                auto rep = std::static_pointer_cast<CcuRep::CcuRepLocWaitEvent>(base);
                const uint16_t actual = static_cast<uint16_t>(Resource(ops_.ckeQuery, rep->GetEventId()));
                EmitWait(base, rep->GetEventId(), rep->GetMask(), true);
                for (uint32_t bit = 1; bit != 0 && bit <= UINT16_MAX; bit <<= 1U) {
                    // 还有信号位没被置上，故障很可能就卡在等这些信号上，把它们依赖的 REP 也补发出来
                    if ((rep->GetMask() & bit) != 0 && (actual & bit) == 0) {
                        for (const auto &dependency : rep->GetDependencyInfo(bit)) {
                            EmitRep(dependency);
                        }
                    }
                }
                break;
            }
            case CcuRepType::LOC_WAIT_NOTIFY: {
                auto rep = std::static_pointer_cast<CcuRep::CcuRepLocWaitNotify>(base);
                EmitWait(base, rep->GetNotifyId(), rep->GetMask(), true); break;
            }
            case CcuRepType::REM_POST_SEM:
                EmitRemoteWait(base, std::static_pointer_cast<CcuRep::CcuRepRemPostSem>(base), false); break;
            case CcuRepType::REM_WAIT_SEM:
                EmitRemoteWait(base, std::static_pointer_cast<CcuRep::CcuRepRemWaitSem>(base), true); break;
            case CcuRepType::REM_POST_VAR: EmitRemotePostVar(base); break;
            case CcuRepType::RECORD_SHARED_NOTIFY: {
                auto rep = std::static_pointer_cast<CcuRep::CcuRepRecordSharedNotify>(base);
                EmitWait(base, rep->GetNotifyId(), rep->GetMask(), false); break;
            }
            case CcuRepType::READ: EmitTransfer(base, std::static_pointer_cast<CcuRep::CcuRepRead>(base)); break;
            case CcuRepType::WRITE: EmitTransfer(base, std::static_pointer_cast<CcuRep::CcuRepWrite>(base)); break;
            case CcuRepType::LOCAL_CPY:
            case CcuRepType::LOCAL_REDUCE: EmitLocalCopy(base); break;
            case CcuRepType::BUF_READ: EmitBufferRead(base); break;
            case CcuRepType::BUF_WRITE: EmitBufferWrite(base); break;
            case CcuRepType::BUF_LOC_READ: EmitBufferLocalRead(base); break;
            case CcuRepType::BUF_LOC_WRITE: EmitBufferLocalWrite(base); break;
            case CcuRepType::BUF_REDUCE: EmitBufferReduce(base); break;
            default: { auto record = Base(base, HCOMM_CCU_DFX_RECORD_DEFAULT); Emit(record); break; }
        }
    }

    const HcommCcuDfxRequestPod &request_;
    HcommCcuDfxEmitFn emit_;
    void *context_;
    const HcommCcuControlOpsPod &ops_;
    int32_t firstError_{0};
};

} // namespace

// 诊断入口（hcomm 控制面在任务异常时跨 SO 调过来）：先校验参数，
// 再取本设备的内核管理器和 hcomm 注入的查询接口，交给 DfxBuilder 生成全部记录，
// 逐条经 emit 回调发出；出错一律返回内部错误，参数不对返回参数错误
int32_t HcommCcuDiagnose(const HcommCcuDfxRequestPod *request, HcommCcuDfxEmitFn emit, void *context)
{
    if (request == nullptr || emit == nullptr || request->deviceId < 0 || request->kernelHandle == 0) {
        return static_cast<int32_t>(CcuResult::CCU_E_PARA);
    }
    try {
        CcuKernelMgr &manager = CcuKernelMgr::GetInstance(request->deviceId);
        return DfxBuilder(*request, emit, context, manager.GetControlOps()).Run();
    } catch (...) {
        return static_cast<int32_t>(CcuResult::CCU_E_INTERNAL);
    }
}

} // namespace asccomm
