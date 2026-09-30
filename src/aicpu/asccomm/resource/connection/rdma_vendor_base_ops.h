/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef RDMA_BASE_VENDOR_OPS_H
#define RDMA_BASE_VENDOR_OPS_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <securec.h>

#include "../../common/utils/exception_util.h"
#include "../../common/exception/internal_exception.h"
#include "asccomm_log.h"
#include "../buffer/rma_buf_slice_lite.h"
#include "../buffer/rmt_rma_buf_slice_lite.h"
#include "asccomm_backend_types.h"

namespace Asc {

struct RdmaSqContextLite {
    uint32_t qpn;
    uint64_t sqVa;
    uint32_t wqeSize;
    uint32_t depth;
    uint64_t headAddr;
    uint64_t tailAddr;
    uint64_t dbHwVa;
    uint64_t dbSwVa;
    uint8_t sl;
    uint64_t dbVendorSpecified;
};

struct RdmaCqContextLite {
    uint32_t cqn;
    uint64_t cqVa;
    uint32_t cqeSize;
    uint32_t cqDepth;
    uint64_t headAddr;
    uint64_t tailAddr;
    uint64_t dbHwVa;
    uint64_t dbSwVa;
};

// Necessary helper funcs
constexpr uint32_t BITS_1BYTE = 8;
constexpr uint32_t BITS_3BYTE = 24;
constexpr uint32_t BITS_5BYTE = 40;
constexpr uint32_t BITS_7BYTE = 56;

inline uint16_t htons16(uint16_t x) { return (((x & 0xffULL) << BITS_1BYTE) | ((x & 0xff00ULL) >> BITS_1BYTE)); }

inline uint32_t htonl32(uint32_t x)
{
    return ((x & 0x000000ffU) << BITS_3BYTE) | ((x & 0x0000ff00U) << BITS_1BYTE) | ((x & 0x00ff0000U) >> BITS_1BYTE) |
           ((x & 0xff000000U) >> BITS_3BYTE);
}

inline uint64_t htonll64(uint64_t x)
{
    return ((x & 0x00000000000000ffULL) << BITS_7BYTE) | ((x & 0x000000000000ff00ULL) << BITS_5BYTE) |
           ((x & 0x0000000000ff0000ULL) << BITS_3BYTE) | ((x & 0x00000000ff000000ULL) << BITS_1BYTE) |
           ((x & 0x000000ff00000000ULL) >> BITS_1BYTE) | ((x & 0x0000ff0000000000ULL) >> BITS_3BYTE) |
           ((x & 0x00ff000000000000ULL) >> BITS_5BYTE) | ((x & 0xff00000000000000ULL) >> BITS_7BYTE);
}

enum class CqPollStatus : int32_t {
    SUCCESS = 0,  /* indicate poll once cqe successfully; */
    EMPTY = -1,   /* indicate the cq is empty when poll cq; */
    ERROR = -2,   /* indicate the error when poll cq; */
    REROLL = -3,  /* 返回到repoll标识 */
    CONTINUE = 1, /* indicate continue the process */
};

class RdmaBaseOps {
public:
    RdmaBaseOps(RdmaSqContextLite* sqContext, RdmaCqContextLite* cqContext)
        : sqContext_(sqContext), cqContext_(cqContext)
    {}
    virtual ~RdmaBaseOps() = default;

    void import_queue_state(uint32_t sqHead, uint32_t sqTail, uint32_t cqHead, uint32_t cqTail, bool cqDbFlush)
    {
        sqHead_ = sqHead;
        sqTail_ = sqTail;
        cqHead_ = cqHead;
        cqTail_ = cqTail;
        cqDbFlush_ = cqDbFlush;
    }

    void export_queue_state(
        uint32_t& sqHead, uint32_t& sqTail, uint32_t& cqHead, uint32_t& cqTail, bool& cqDbFlush) const
    {
        sqHead = sqHead_;
        sqTail = sqTail_;
        cqHead = cqHead_;
        cqTail = cqTail_;
        cqDbFlush = cqDbFlush_;
    }

    // 上层接口，不关心具体vendor类型
    AsccommResult read(const RmaBufSliceLite& loc, const RmtRmaBufSliceLite& rmt, const SqeConfigLite& cfg)
    {
        // read需要占用1个wr位置, 确定Sq存在空位
        constexpr int ReadWqeCount = 1;
        CHK_RET(wait_sq_free(ReadWqeCount));

        // 进入vendor特有wqe组装接口, 组装完wqe直接下发, opCode不支持直接返回ASCCOMM_E_NOT_SUPPORT
        CHK_RET(build_read_wqe(loc, rmt, cfg));

        // 更新Sq队列PI值
        CHK_RET(update_sq_pi());

        return ASCCOMM_SUCCESS;
    }

    AsccommResult write(const RmaBufSliceLite& loc, const RmtRmaBufSliceLite& rmt, const SqeConfigLite& cfg)
    {
        // write需要占用1个wr位置, 确定Sq存在空位
        constexpr int WriteWqeCount = 1;
        CHK_RET(wait_sq_free(WriteWqeCount));

        // 进入vendor特有wqe组装接口, 组装完wqe直接下发
        CHK_RET(build_write_wqe(loc, rmt, cfg));

        // 更新Sq队列PI值
        CHK_RET(update_sq_pi());

        return ASCCOMM_SUCCESS;
    }

    AsccommResult write_reduce(
        const RmaBufSliceLite& loc, const RmtRmaBufSliceLite& rmt, const SqeConfigLite& cfg, DataType dataType,
        ReduceOp reduceOp)
    {
        // Inline Reduce write需要占用1个wr位置, 确定Sq存在空位
        constexpr int WriteReduceWqeCount = 1;
        CHK_RET(wait_sq_free(WriteReduceWqeCount));

        // 进入vendor特有wqe组装接口, 组装完wqe直接下发
        CHK_RET(build_write_reduce_wqe(loc, rmt, cfg, dataType, reduceOp));

        // 更新Sq队列PI值
        CHK_RET(update_sq_pi());

        return ASCCOMM_SUCCESS;
    }

    AsccommResult poll_cq(int32_t numEntries, int32_t timeOut, std::vector<int32_t>& errList)
    {
        auto timeLimit = std::chrono::milliseconds(timeOut);
        auto startTime = std::chrono::steady_clock::now();

        int32_t totalPollNum = 0;
        int32_t ret = 0;

        while (totalPollNum < numEntries) {
            // 逐一Poll Cq，并处理cqe
            ret = poll_cq_impl(numEntries - totalPollNum, errList);
            if (ret == static_cast<int32_t>(CqPollStatus::ERROR)) {
                ASCCOMM_ERROR("[RdmaBaseOps::%s][Poll cq] Poll Cq Error.", __func__);
                return ASCCOMM_E_REMOTE;
            }

            if (ret > 0) {
                // Update cqe in this loop
                totalPollNum += ret;

                // Update Sq Tail
                sqTail_ += ret;

                // continue poll cqe
                continue;
            }

            if ((std::chrono::steady_clock::now() - startTime) > timeLimit) {
                ASCCOMM_ERROR(
                    "[RdmaBaseOps::%s][Poll cq] Poll Cq timeout, expected[%d], actual[%d], lastRet[%d]", __func__,
                    numEntries, totalPollNum, ret);
                return ASCCOMM_E_TIMEOUT;
            }
        }

        ASCCOMM_INFO(
            "[RdmaBaseOps::%s][Poll cq] Poll Cq success, expected[%d], actual[%d]", __func__, numEntries, totalPollNum);
        return ASCCOMM_SUCCESS;
    }

    // 准备Doorbell(厂商实现)
    virtual AsccommResult build_doorbell(uint64_t& dbAddr, uint64_t& dbValue) = 0;

    // 准备CqDoorbell(厂商实现)
    virtual AsccommResult build_cq_doorbell(uint64_t& dbAddr, uint64_t& dbValue) = 0;

protected:
    // 软件侧只维护Sq PI，Sq CI由硬件维护
    uint32_t sqHead_{0};
    uint32_t sqTail_{0};

    uint32_t cqHead_{0};
    uint32_t cqTail_{0};
    bool cqDbFlush_ = false;

    RdmaSqContextLite* sqContext_;
    RdmaCqContextLite* cqContext_;

    // 默认超时时间 30 ms
    const std::chrono::milliseconds timeout_ = std::chrono::milliseconds(30U);

    // vendor扩展点: 每个原子op一个虚函数
    // 默认 NOT_SUPPORT, 各个vendor 只重写自己支持的
    virtual AsccommResult build_read_wqe(
        const RmaBufSliceLite& loc, const RmtRmaBufSliceLite& rmt, const SqeConfigLite& cfg)
    {
        (void)loc;
        (void)rmt;
        (void)cfg;
        ASCCOMM_ERROR("[RdmaBaseOps::%s] This Backend Not support read Now.", __func__);
        return ASCCOMM_E_NOT_SUPPORT;
    }

    virtual AsccommResult build_write_wqe(
        const RmaBufSliceLite& loc, const RmtRmaBufSliceLite& rmt, const SqeConfigLite& cfg)
    {
        (void)loc;
        (void)rmt;
        (void)cfg;
        ASCCOMM_ERROR("[RdmaBaseOps::%s] This Backend Not support write Now.", __func__);
        return ASCCOMM_E_NOT_SUPPORT;
    }

    virtual AsccommResult build_write_reduce_wqe(
        const RmaBufSliceLite& locNotify, const RmtRmaBufSliceLite& notify, const SqeConfigLite& cfg, DataType dataType,
        ReduceOp reduceOp)
    {
        (void)locNotify;
        (void)notify;
        (void)cfg;
        (void)dataType;
        (void)reduceOp;
        ASCCOMM_ERROR("[RdmaBaseOps::%s] This Backend Not support write_reduce Now.", __func__);
        return ASCCOMM_E_NOT_SUPPORT;
    }

    virtual AsccommResult write_invalid_wqebb(uint32_t nextIdx)
    {
        (void)nextIdx;
        return ASCCOMM_SUCCESS;
    }

    virtual int32_t poll_cq_impl(int32_t numEntries, std::vector<int32_t>& errList)
    {
        (void)numEntries;
        ASCCOMM_ERROR("[RdmaBaseOps::%s] This Backend Not support poll_cq Now.", __func__);
        return ASCCOMM_E_NOT_SUPPORT;
    }

    // 搬运wqe(通用实现), 把 Wqe 写到 SQ
    AsccommResult commit_wqe(const void* wqe, uint32_t wqeSize)
    {
        ASCCOMM_INFO("[RdmaBaseOps::%s] Memcpy wqe start, Now SQ PI: [%u]", __func__, sqHead_);

        // 写wqe到va
        auto sqDepth = sqContext_->depth;
        uint32_t sqPIMask = sqDepth - 1;
        uint8_t* va = reinterpret_cast<uint8_t*>(sqContext_->sqVa + (sqHead_ & sqPIMask) * wqeSize);

        ASCCOMM_INFO(
            "[RdmaBaseOps][Wqe write] before copy, sqHead[%u], slot[%u], sqVa[0x%llx], dst[0x%llx], size[%u]", sqHead_,
            sqHead_ & sqPIMask, static_cast<unsigned long long>(sqContext_->sqVa),
            reinterpret_cast<unsigned long long>(va), wqeSize);

        auto ret = memcpy_sp(va, wqeSize, wqe, wqeSize);
        if (UNLIKELY(ret != 0)) {
            throw_exception<InternalException>(
                string_format("[RdmaBaseOps::%s] memcpy_s failed, ret = %d", __func__, ret));
        }

        // pi维护用于传入DB Send用于Rtsq 敲door bell
        sqHead_ = sqHead_ + 1;

        // write InValid Wqebb
        CHK_RET(write_invalid_wqebb(sqHead_));

        ASCCOMM_INFO("[RdmaBaseOps::%s] Memcpy wqe end, Now SQ PI: [%u]", __func__, sqHead_);
        return ASCCOMM_SUCCESS;
    }

    AsccommResult wait_sq_free(uint32_t wqeNum)
    {
        // wq_overflow
        bool timeOutFlag = false;
        auto startTime = std::chrono::steady_clock::now();

        ASCCOMM_INFO("[RdmaBaseOps::%s] Operate: sqTail = %u", __func__, sqTail_);
        while (!timeOutFlag) {
            // sq 队列能放下，直接成功返回
            if (static_cast<uint32_t>(sqHead_ - sqTail_ + wqeNum) <= sqContext_->depth) {
                return ASCCOMM_SUCCESS;
            }

            timeOutFlag = (std::chrono::steady_clock::now() - startTime) > timeout_;
        }

        // 超时处理
        ASCCOMM_ERROR("[RdmaBaseOps::%s] Sq is Full !! Operate: sqTail = %u Failed. ", __func__, sqTail_);
        return ASCCOMM_E_TIMEOUT;
    }

    // 将PI更新到硬件可见地址
    AsccommResult update_sq_pi()
    {
        // 更新Sq PI指针
        uint32_t sqHeadNum = htonl32(sqHead_);

        ASCCOMM_INFO(
            "[RdmaBaseOps][Wqe write] write soft PI, sqHead host[%u], dbSwVa[0x%llx]", sqHead_,
            static_cast<unsigned long long>(sqContext_->dbSwVa));

        auto status =
            memcpy_sp(reinterpret_cast<void*>(sqContext_->dbSwVa), sizeof(uint32_t), &sqHeadNum, sizeof(uint32_t));
        if (UNLIKELY(status != 0)) {
            throw_exception<InternalException>(
                string_format("[RdmaBaseOps::%s] Ring Sw DB failed, ret = %d", __func__, status));
        }
        return ASCCOMM_SUCCESS;
    }
};

} // namespace Asc
#endif // RDMA_BASE_VENDOR_OPS_H
