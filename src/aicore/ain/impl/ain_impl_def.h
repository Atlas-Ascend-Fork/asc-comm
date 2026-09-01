/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file ain_impl_def.h
 * \brief Ain implementation definition
 */

#if !defined(__ASCENDC_INCLUDE_INTERNAL_HEADERS__)
#pragma message("This is an internal Ain header file and must not be used directly. " \
                "Please use public interface headers.")
#define __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#define __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_IMPL_DEF_H__
#endif

#ifndef IMPL_COMM_API_AICORE_AIN_IMPL_AIN_IMPL_DEF_H
#define IMPL_COMM_API_AICORE_AIN_IMPL_AIN_IMPL_DEF_H

#include "../../hcomm/impl/hcomm_impl_def.h"
// #include "hcomm/hcomm_team_entity_defs.h"

namespace AscendC {

typedef struct {
    CommAbiHeader header;
    struct {
        uint64_t baseRemoteMemAddr; /* 远端内存addr数组基地址，按(worldTeamAccumulateId[netLayer] + worldTeamId) *
                                       sizeof(uint64_t)偏移访问 */
        uint64_t windowSize;        /* window大小 */
        uint32_t* worldTeamAccumulateId; /* 数组，表示sum(worldTeamSizePerNetLayer[0..netLayerNum-1]) */
        uint32_t netLayerNum;            /* netLayer的数量 */
        uint32_t reserved[8];
    } netWin;

    struct {
        uint64_t baseVa;
        uint64_t stride;
        uint64_t userSize;
        uint32_t reserved[8];
    } lsaWin;

    uint64_t legacySymWindow; // SymmetricWindow结构体device侧指针
    uint32_t reserved[8];
} HcommWindow;

typedef struct {
    CommMem* remoteMems; // 从远端交换过来的内存,
                         // 用作signal/barrier等，如果memberId是selfMemberId，则是本地内存，否则是远端内存
    uint32_t remoteMemsNum;
    CommMem
        shadowMem; // 申请一块(signalCount + counterCount + barrierCount) * sizeof(uint64_t) * memberNum的device侧内存
    HcommTeamSyncMemRequirement syncMemReq;
    uint64_t syncMemSize; // (signalCount + counterCount + barrierCount) * sizeof(uint64_t) * memberNum
    uint32_t reserved[5];
} HcommTeamSyncMem;

// hcommTeamRes存放worldTeam的资源，使用subTeam来寻找hcommTeamRes
typedef struct {
    CommAbiHeader header;
    CommEngine engine; /* COMM_ENGINE_AICPU_TS / CCU 等 */
    uint32_t memberNum;
    uint32_t selfMemberId;
    uint64_t channelsBaseAddr; /* 连续ChannelEntity数组的基地址，按(channelCntAccumulatePerMember[peer] + channelIdx) *
                                  sizeof(ChannelEntity)偏移访问 */
    uint32_t* channelCntAccumulatePerMember; /* 长度为memberNum，表示sum(channelNumPerMember[0..peer-1]) */
    uint32_t netLayer;                       /* 用户希望使用的网络层，0表示默认选择 */
    uint32_t* worldTeamIds;
    HcommTeamSyncMem syncMem;
    uint32_t reserved[8];
} HcommTeam;

static constexpr struct UrmaWqeEntry SIGNAL_WQE_CONFIG = {
    .odr = 6,
    .fence = 1,
    .se = 0,
    .cqe = 0,
    .inlineEn = 0,
}; 

constexpr CommProtocol MaskToCommProtocol(unsigned commEngineMask)
{
    switch (commEngineMask) {
        case AIN_MASK_DEFAULT:
            return COMM_PROTOCOL_UBC_CTP;
        default:
            return COMM_PROTOCOL_RESERVED;
    }
}

template <unsigned CommEngineMask>
class Ain;

template <unsigned CommEngineMask>
class AinBarrierSessionImpl {
public:
    static constexpr CommProtocol commProtocol = MaskToCommProtocol(CommEngineMask);
    template <bool EnableTimeout, typename DescriptorUbuf>
    AIN_DEVICE int32_t SyncImpl(AinMemoryOrder order, uint64_t timeoutCycles, const DescriptorUbuf& ubuf);

private:
    template <typename DescriptorUbuf>
    AIN_DEVICE void Signal(HcommTeamHandle team, uint32_t peer, const DescriptorUbuf& ubuf);

    AIN_DEVICE uint64_t ReadSignal(
        HcommTeamHandle team, uint32_t signalId, uint32_t bits = 64,
        AinMemoryOrder order = AIN_MEMORY_ORDER_RELAX) const;

    AIN_DEVICE void WaitSignal(
        HcommTeamHandle team, uint32_t signalId, uint64_t least, uint32_t bits = 64,
        AinMemoryOrder order = AIN_MEMORY_ORDER_RELAX) const;

public:
    Ain<CommEngineMask>* ain_;
    HcommTeamHandle team_;
    uint32_t index_;
    size_t signalBaseId_;
    HcommImpl<commProtocol> hcomm_;
};

} // namespace AscendC

#endif // IMPL_COMM_API_AICORE_AIN_IMPL_AIN_IMPL_DEF_H

#if defined(__UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_IMPL_DEF_H__)
#undef __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#undef __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_IMPL_DEF_H__
#endif
