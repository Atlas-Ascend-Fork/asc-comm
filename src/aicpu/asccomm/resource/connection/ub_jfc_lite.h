/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software: you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_UB_JFC_LITE_H
#define ASCCOMM_UB_JFC_LITE_H

#include <chrono>
#include <functional>
#include "asccomm_res_defs.h"
#include "udma_data_struct.h"
#include "../../common/utils/asccomm_result.h"
#include "securec.h"
#include "asccomm_log.h"

namespace Asc {

// JFC完成队列软件轮询器。与UbConnLite同模式的无状态对象：
// 每次调用栈上构造，消费游标cqTail经import/export与持久的HcommCqContext(设备内存)交换。
class UbJfcLite {
public:
    void import(const HcommCqContext& cq)
    {
        const auto& ubJfc = cq.contextInfo.ubJfc;
        scqVa_ = ubJfc.scqVa;
        cqeSize_ = ubJfc.cqeSize;
        cqDepth_ = ubJfc.cqDepth;
        cqTail_ = ubJfc.cqTail;
    }

    void export_state(HcommCqContext& cq) const { cq.contextInfo.ubJfc.cqTail = cqTail_; }

    // 轮询一条接收侧CQE。got=false表示暂无新CQE(未超时)；ret非0表示错误CQE或超时。
    // 自旋等待期间按心跳间隔回调tryLaunch，以便SQ满时推进已积压的SQE
    // (复用asccomm_runtime ensure_queue_space的协同模式)。
    AsccommResult poll_one(uint32_t timeoutSeconds, UdmaCqe& cqe, bool& got)
    {
        got = false;
        auto startTime = std::chrono::steady_clock::now();
        uint32_t retryCnt = 0;
        while (true) {
            UdmaCqe* cqeAddr = current_cqe();
            if (cqeAddr != nullptr && owner_match(*cqeAddr)) {
                auto ret = memcpy_sp(&cqe, sizeof(cqe), cqeAddr, sizeof(cqe));
                CHK_PRT_RET(
                    ret != 0, ASCCOMM_ERROR("[UbJfcLite::%s] memcpy_sp cqe failed, ret = %d.", __func__, ret),
                    ASCCOMM_E_INTERNAL);
                CHK_RET(validate_cqe(cqe));
                cqTail_++;
                got = true;
                return ASCCOMM_SUCCESS;
            }
            const bool timeout = timeoutSeconds != 0 &&
                                 std::chrono::steady_clock::now() - startTime >= std::chrono::seconds(timeoutSeconds);
            CHK_PRT_RET(
                timeout,
                ASCCOMM_ERROR(
                    "[UbJfcLite::%s] poll cqe timeout, cqTail[%u], cqDepth[%u].", __func__, cqTail_, cqDepth_),
                ASCCOMM_E_TIMEOUT);
            if (++retryCnt % POLL_CQ_HEARTBEAT_TIMES == 0) {
                ASCCOMM_WARNING("[UbJfcLite::%s] polling cqe, cqTail[%u], retry[%u].", __func__, cqTail_, retryCnt);
            }
            tryLaunch();
        }
    }

    // 传输层注入的SQE推进钩子(批量模式下清积压)；默认无操作。
    std::function<void()> tryLaunch = []() {};

private:
    UdmaCqe* current_cqe() const
    {
        if (scqVa_ == 0 || cqeSize_ == 0 || cqDepth_ == 0) {
            return nullptr;
        }
        return reinterpret_cast<UdmaCqe*>(scqVa_ + static_cast<uint64_t>(cqTail_ & (cqDepth_ - 1)) * cqeSize_);
    }

    // owner相位位判圈：第N圈轮询时期望的owner位为(N/cqDepth)&1。
    // 逻辑对齐aicore侧PollCqImpl(hcomm_aiv_urma.h)与RoCE侧roce3_get_one_cqe的owner校验。
    bool owner_match(const UdmaCqe& cqe) const
    {
        uint32_t validOwner = (cqTail_ / cqDepth_) & 1U;
        return (validOwner ^ cqe.owner) != 0U;
    }

    AsccommResult validate_cqe(const UdmaCqe& cqe) const
    {
        CHK_PRT_RET(
            cqe.fd != 0, ASCCOMM_ERROR("[UbJfcLite::%s] flushed CQE, substatus[%u].", __func__, cqe.substatus),
            ASCCOMM_E_REMOTE);
        CHK_PRT_RET(
            cqe.status != 0 || cqe.substatus != 0,
            ASCCOMM_ERROR("[UbJfcLite::%s] CQE error, status[%u], substatus[%u].", __func__, cqe.status, cqe.substatus),
            ASCCOMM_E_REMOTE);
        return ASCCOMM_SUCCESS;
    }

    static constexpr uint32_t POLL_CQ_HEARTBEAT_TIMES = 100000U; // 心跳日志间隔(约100ms量级)

    uint64_t scqVa_{0};
    uint32_t cqeSize_{0};
    uint32_t cqDepth_{0};
    uint32_t cqTail_{0};
};

} // namespace Asc

#endif // ASCCOMM_UB_JFC_LITE_H
