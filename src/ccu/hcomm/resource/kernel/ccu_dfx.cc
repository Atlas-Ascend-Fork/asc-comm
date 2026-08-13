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

// loop 实例参数在 XN（变量槽）中的位域布局
struct LoopXm {
    uint64_t loopCnt : 13;
    uint64_t gsaStride : 32;
    uint64_t loopCtxId : 8;
    uint64_t reserved : 11;
};

// loop 组参数在 XN 中的位域布局：loopInsCnt 组内 loop 数 / expandOffset 与 expandCnt 为展开窗口
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

// 诊断记录构建器：给定故障请求（device/die/mission/kernel），通过 controlOps 函数指针
// （hcomm 控制面实现）查询 mission/loop/资源运行现场，把故障指令附近每条 REP 展开为
// 一条 HcommCcuDfxRecordPod，逐条经 emit 回调送出；数据面自身不直接访问驱动。
class DfxBuilder {
public:
    DfxBuilder(const HcommCcuDfxRequestPod &request, HcommCcuDfxEmitFn emit, void *context,
        const HcommCcuControlOpsPod &ops)
        : request_(request), emit_(emit), context_(context), ops_(ops)
    {
    }

    // 诊断主流程：查 mission 上下文 → 定位内核 → 先发 MISSION 记录，再展开当前指令
    // （LOOPGROUP 场景展开整组 loop），最后按 10 条回看窗口逐条展开故障前的 REP；
    // 返回首个 emit 错误码（未出错返回 0，现场无效返回 CCU_E_NOT_FOUND）
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

        // 回看窗口：往前至多 10 条指令，且不越过本条 mission 的起始槽位
        const uint16_t distance = 10;
        uint16_t begin = mission.currentInstructionId > distance ? mission.currentInstructionId - distance : 0;
        begin = std::max(begin, mission.startInstructionId);
        for (int32_t id = mission.currentInstructionId; id >= begin; --id) {
            if (kernel->GetRepByInstrId(static_cast<uint16_t>(id)) == nullptr) {
                // 该槽位没挂任何 REP（指令数组空洞），窗口收窄到这里
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
    // 构造记录公共骨架：类型 + REP 类型稳定值 + die/mission + instrId（取 REP 起始槽位）
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

    // 送出记录并锁存首个回调错误：只保留第一次失败，后续记录不再回调，保证错误码确定
    void Emit(HcommCcuDfxRecordPod &record)
    {
        if (firstError_ == 0) {
            firstError_ = emit_(context_, &record);
        }
    }

    // 按资源 id 查询 XN/CKE/GSA 的当前值（走 controlOps）；失败返回 UINT64_MAX 哨兵并锁存 CCU_E_INTERNAL
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

    // 解析 channel 句柄为记录可用的数据面 channelId；失败时 id 保留 UINT16_MAX 哨兵
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

    // 指令槽位号 → 叶子 REP 解析：先按槽位区间在 kernel 主序列中定位所属 REP，
    // 命中 FUNC_BLOCK（函数调用容器，吞槽位但无自身搬运/同步语义）时循环下钻到
    // 块内子 REP，直到拿到可解释的叶子 REP；找不到返回 nullptr 由调用方判定现场无效
    std::shared_ptr<CcuRepBase> ResolveRep(CcuKernel &kernel, uint16_t id)
    {
        auto rep = kernel.GetRepByInstrId(id);
        while (rep != nullptr && rep->Type() == CcuRepType::FUNC_BLOCK) {
            // FUNC_BLOCK 只是函数调用容器，真正的操作在内部子 REP 里，继续按槽位下钻
            rep = std::static_pointer_cast<CcuRep::CcuRepBlock>(rep)->GetRepByInstrId(id);
        }
        return rep;
    }

    // 发 MISSION 记录：声明本次诊断的 mission 与当前指令槽位，供 hcomm 侧定位指令区间
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

    // 发 WAIT_SIGNAL 记录：本端事件/信号量等待现场（signalId/mask；readValue 时读实际信号值）
    void EmitWait(const std::shared_ptr<CcuRepBase> &base, uint16_t signal, uint16_t mask, bool readValue)
    {
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_WAIT_SIGNAL);
        record.msg.waitSignal.signalId = signal;
        record.msg.waitSignal.signalMask = mask;
        record.msg.waitSignal.signalValue = readValue ? static_cast<uint16_t>(Resource(ops_.ckeQuery, signal)) : 0;
        // 未用的 channel 槽位置哨兵 UINT16_MAX，便于 hcomm 侧识别
        std::fill(std::begin(record.msg.waitSignal.channelId), std::end(record.msg.waitSignal.channelId), UINT16_MAX);
        Emit(record);
    }

    template <typename Rep>
    // 发 WAIT_SIGNAL 记录：远端 POST/WRITE 信号现场——远端 CKE id 经 channel 快照解析，
    // readValue 决定是否再读一次信号实际值
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

    // 发 WAIT_SIGNAL 记录：RV（远程 POST 变量）现场——远端 CKE + 远程参数槽 XN id 及其当前值
    void EmitRemotePostVar(const std::shared_ptr<CcuRepBase> &base)
    {
        auto rep = std::static_pointer_cast<CcuRep::CcuRepRemPostVar>(base);
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_WAIT_SIGNAL);
        SetChannel(rep->GetChannel(), record.msg.waitSignal.channelId[0], record.msg.waitSignal.channelHandle[0]);
        std::fill(std::begin(record.msg.waitSignal.channelId) + 1, std::end(record.msg.waitSignal.channelId), UINT16_MAX);
        record.msg.waitSignal.signalId = ChannelSignal(rep->GetChannel(), rep->GetSemIndex(), true);
        record.msg.waitSignal.signalMask = rep->GetMask();
        // 变量槽 id 在对端 channel 的 XN 数组里，变量当前值再经 XN 查询读出
        record.msg.waitSignal.paramId = ChannelRemoteXn(rep->GetChannel(), rep->GetParamIndex());
        record.msg.waitSignal.paramValue = Resource(ops_.xnQuery, rep->GetParam().Id());
        Emit(record);
    }

    // 发 TRANSFER 记录：READ/WRITE 搬运现场（本端/远端地址=GSA、token/长度=XN、信号量、channel）
    template <typename Rep>
    void EmitTransfer(const std::shared_ptr<CcuRepBase> &base, const std::shared_ptr<Rep> &rep)
    {
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_TRANSFER);
        // 地址走 GSA 查询、token/长度走 XN 查询，把资源 id 还原成真实值
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

    // 发 TRANSFER 记录：本地复制/归约现场（源/目的均为本端地址与 token，不经 channel）
    void EmitLocalCopy(const std::shared_ptr<CcuRepBase> &base)
    {
        auto rep = std::static_pointer_cast<CcuRep::CcuRepLocCpy>(base);
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_TRANSFER);
        // 本地复制仍复用 TRANSFER 的源/目的槽位表达（两端都是本端内存）
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

    // 发 BUFFER_TRANSFER 记录：缓冲区读（源为内存地址，目的为 buffer id）
    void EmitBufferRead(const std::shared_ptr<CcuRepBase> &base)
    {
        auto rep = std::static_pointer_cast<CcuRep::CcuRepBufRead>(base);
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_BUFFER_TRANSFER);
        record.msg.bufTransMem.bufId = rep->GetDstAddrId() & 0x7fffU; // 掩掉 bit15（IO Die 标志位）只留 buffer id
        record.msg.bufTransMem.addr = Resource(ops_.gsaQuery, rep->GetSrcAddrId());
        record.msg.bufTransMem.token = Resource(ops_.xnQuery, rep->GetSrcTokenId());
        record.msg.bufTransMem.len = Resource(ops_.xnQuery, rep->GetLenId());
        record.msg.bufTransMem.signalId = rep->GetSemId();
        record.msg.bufTransMem.signalMask = rep->GetMask();
        SetChannel(rep->GetChannel(), record.msg.bufTransMem.channelId, record.msg.bufTransMem.channelHandle);
        Emit(record);
    }

    // 发 BUFFER_TRANSFER 记录：缓冲区写（源为 buffer id，目的为内存地址）
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

    // 发 BUFFER_TRANSFER 记录：本地缓冲区读（不经 channel，channelId 置 UINT16_MAX 哨兵）
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

    // 发 BUFFER_TRANSFER 记录：本地缓冲区写（不经 channel，channelId 置 UINT16_MAX 哨兵）
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

    // 发 BUFFER_REDUCE 记录：多缓冲区归约现场（参与 buffer id 列表、count/dtype/optype、XN 长度 id）
    void EmitBufferReduce(const std::shared_ptr<CcuRepBase> &base)
    {
        auto rep = std::static_pointer_cast<CcuRep::CcuRepBufReduce>(base);
        auto record = Base(base, HCOMM_CCU_DFX_RECORD_BUFFER_REDUCE);
        // 定长数组先全置哨兵，再回填实际参与的 buffer id（同样掩掉 bit15 标志位）
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

    // 发 LOOP 记录并展开 loop 体：从 XN 解 loop 参数（loopCtxId/count），查 loop 上下文得
    // 当前次数与地址步长，再对 loop 体每个指令槽位逐条 EmitRep
    void EmitLoop(CcuKernel &kernel, uint16_t instructionId)
    {
        auto base = kernel.GetRepByInstrId(instructionId);
        if (base == nullptr || base->Type() != CcuRepType::LOOP) {
            return;
        }
        auto rep = std::static_pointer_cast<CcuRep::CcuRepLoop>(base);
        // loop 参数编码在 XN 槽里（位域：次数/步长/loop 上下文 id）
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
        // 把 loop 体内每个槽位的 REP 也展开（在 loop block 内再次按区间查找）
        for (uint32_t id = record.msg.loop.startInstrId; id <= record.msg.loop.endInstrId; ++id) {
            EmitRep(rep->GetLoopBlock()->GetRepByInstrId(static_cast<uint16_t>(id)));
        }
    }

    // 发 LOOP_GROUP 记录并展开整组 loop：从 XN 解 loopInsCnt/expandOffset/expandCnt，逐个 EmitLoop
    void EmitLoopGroup(CcuKernel &kernel, const std::shared_ptr<CcuRepBase> &base)
    {
        if (base == nullptr) {
            return;
        }
        auto rep = std::static_pointer_cast<CcuRep::CcuRepLoopGroupBundle>(base);
        // 组参数也在 XN 槽里（组内 loop 数/展开偏移/展开次数）
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

    // 按 REP 类型分派为对应记录；LOC_WAIT_EVENT 存在未满足的 event 依赖位时，
    // 按依赖信息补发前置 REP；未知类型落 DEFAULT 记录兜底
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
                    // 等待位里仍有未置起的信号，故障可能就卡在对应的前置 event，顺藤摸瓜补发
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

// 诊断入口（hcomm 控制面经 taskParam_.ccuDiagnose 跨 SO 调用）：校验请求参数后，
// 取该设备 Kernel 管理器与注入的控制面函数表，交给 DfxBuilder 展开全部记录并逐条
// 经 emit 回调送出；异常兜底为 CCU_E_INTERNAL，参数非法返回 CCU_E_PARA。
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
