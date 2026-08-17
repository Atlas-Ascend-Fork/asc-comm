/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_CHANNEL_H
#define ASCCOMM_CCU_CHANNEL_H

#include <cstdint>

#include "hcomm/hcomm_ccu_control.h"
#include "hcomm/hcomm_types.h"

namespace asccomm {

// CcuChannel：CCU Channel 在 asc-comm 数据面侧的本地值对象。
// 构造时通过 hcomm 注入的 channelQuery 一次性获取定长 POD 快照（HcommCcuChannelPod），
// 之后所有资源访问均基于该本地快照，不再触碰 hcomm 的 Channel 对象。
// 该对象被设计为不可拷贝的包装语义：operator-> 直接解引用自身（保留迁移前 channel->GetXxx() 的调用形式）。
class CcuChannel {
public:
    // 构造即发起一次 channelQuery 查询并缓存快照，Init 失败时通过 GetResult 查询错误
    explicit CcuChannel(ChannelHandle channel);

    // 保留迁移前 channel->GetXxx() 的调用形式；当前对象本身已是 asc 本地值对象，不做包装
    CcuChannel *operator->();
    const CcuChannel *operator->() const;
    HcclResult GetResult() const;
    uint32_t GetDieId() const;
    uint32_t GetChannelId() const;
    HcclResult GetLocCkeByIndex(uint32_t index, uint32_t &localCkeId) const;
    HcclResult GetLocXnByIndex(uint32_t index, uint32_t &localXnId) const;
    HcclResult GetRmtCkeByIndex(uint32_t index, uint32_t &remoteCkeId) const;
    HcclResult GetRmtXnByIndex(uint32_t index, uint32_t &remoteXnId) const;
    HcclResult GetRmtBuffer(uint64_t &addr, uint32_t &size, uint32_t &tokenId, uint32_t &tokenValue) const;

private:
    HcclResult Init(ChannelHandle channel);
    // 从 POD 定长数组安全读取第 index 个资源 id（含空指针/越界校验）
    HcclResult GetIdByArray(const uint32_t *ids, uint32_t index, uint32_t &id) const;

    // 单次查询得到的完整 POD 快照；不再持有变长容器，CcuChannel 生命周期与 hcomm 完全解耦
    HcommCcuChannelPod channelPod_{};
    // Init 的结果缓存，初始为内部错误，避免未初始化即被使用
    HcclResult result_{HCCL_E_INTERNAL};
};

} // namespace asccomm

#endif // ASCCOMM_CCU_CHANNEL_H
