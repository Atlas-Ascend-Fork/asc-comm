/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#ifndef UB_MEM_TRANSPORT_LITE_H
#define UB_MEM_TRANSPORT_LITE_H

#include <vector>
#include <memory>
#include <unordered_map>
#include "base_transport_lite_impl.h"
#include "notify_lite.h"
#include "task_param.h"
#include "rmt_rma_buf_slice_lite.h"
#include "rma_conn_lite.h"
#include "kernel_param_lite.h"

namespace Hccl {

class UbTransportLiteImpl : public BaseTransportLiteImpl {
public:
    explicit UbTransportLiteImpl(std::vector<char>                                                 &uniqueId,
                                 std::function<void(u32 streamId, u32 taskId, const TaskParam &taskParam)> callback);

    UbTransportLiteImpl(std::vector<char> &uniqueId);
    void Init(std::vector<char> &uniqueId);

    ~UbTransportLiteImpl() override;

    std::string Describe() const override;

    Buffer GetRmtBuffer(u32 index) override;

    Eid GetLocEid() const;
    Eid GetRmtEid() const;

    void Post(u32 index, const StreamLite &stream) override;

    void Wait(u32 index, const StreamLite &stream) override;

    void Read(const RmaBufferLite &loc, const Buffer &rmt, const StreamLite &stream) override;

    void Write(const RmaBufferLite &loc, const Buffer &rmt, const StreamLite &stream) override;

    void ReadReduce(const RmaBufferLite &loc, const Buffer &rmt, const ReduceIn &reduceIn,
                    const StreamLite &stream) override;

    void WriteReduce(const RmaBufferLite &loc, const Buffer &rmt, const ReduceIn &reduceIn,
                     const StreamLite &stream) override;

    void WriteWithNotify(const RmaBufferLite &loc, const Buffer &rmt, const WithNotifyIn &withNotify,
                         const StreamLite &stream) override;

    void WriteReduceWithNotify(const RmaBufferLite &loc, const Buffer &rmt, const ReduceIn &reduceIn,
                               const WithNotifyIn &withNotify, const StreamLite &stream) override;

    void BatchOneSidedWrite(const vector<RmaBufSliceLite> &loc, const vector<RmtRmaBufSliceLite>  &rmt,
        const StreamLite &stream) override;

    void BatchOneSidedRead(const vector<RmaBufSliceLite> &loc, const vector<RmtRmaBufSliceLite>  &rmt,
        const StreamLite &stream) override;
    
    void BatchTransfer(const std::vector<RmaBufferLite> &loc, const std::vector<Buffer> &rmt,
                        const std::vector<TransferOp> &transferOp, const StreamLite &stream) override;

    HcclResult BuildLocRmaBufferLite(const uintptr_t addr, const size_t size, RmaBufferLite &rmaBufferLite) const;
    HcclResult Fence();

    HcclResult Clean();
    HcclResult Resume(std::vector<char> &uniqueId);

    HcclResult SetAddTaskInfoCallback(std::function<HcclResult(u32, u32, const TaskParam&, u64)> callback); // 自定义算子流程上报task的Callback
private:
    u32 notifyNum{0};
    u32 bufferNum{0};
    u32 connNum{0};
    bool fence_{false};

    struct RmtUbBufLite {
        u64         addr;
        u64         size;
        u32         tokenId;
        u32         tokenValue;
        std::string Describe() const
        {
            return StringFormat("RmtUbBufLite[addr=0x%llx, size=0x%llx]", addr, size);
        }
    };

     struct LocUbBufLite {
        u64         addr;
        u64         size;
        u32         tokenId;
        u32         tokenValue;
        std::string Describe() const
        {
            return StringFormat("LocUbBufLite[addr=0x%llx, size=0x%llx]", addr, size);
        }
    };

    std::vector<char>    wqeData; // connection返回的WQE内容
    ConnLiteOperationOut connOut; // connection的输出

    void ClearConnOut();

    using RmtUbBufLiteVec = std::vector<RmtUbBufLite>;
    using LocUbBufLiteVec = std::vector<LocUbBufLite>;
    MAKE_ENUM(RmaUbBufType, NOTIFY, BUFFER)
    RmtUbBufLiteVec rmtNotifyVec;
    RmtUbBufLiteVec rmtBufferVec;
    LocUbBufLiteVec locBufferVec;

    RmtRmaBufSliceLite GetRmtNotifySliceLite(u32 index);
    RmtRmaBufSliceLite GetRmtRmaBufSliceLite(const Buffer &rmtBuf);

    RmaBufSliceLite GetRmaBufSlicelite(const RmaBufferLite &lite) const;
    RmtRmaBufSliceLite GetRmtRmaBufSliceLite(const RmaBufferLite &lite) const;

    std::vector<std::unique_ptr<NotifyLite>> locNotifyVec;

    // N秒快恢需要清理的两个资源
    std::vector<std::vector<char>> connUniqueIdVec;
    std::vector<RmaConnLite *> connVec;

    std::function<void(u32 streamId, u32 taskId, const TaskParam &taskParam)> callback_{nullptr};
    
    std::function<HcclResult(u32, u32, const TaskParam&, u64)> newCallback_{nullptr};

    void ProfilingProcess(const RmaBufSliceLite &loc, const RmtRmaBufSliceLite &rmt, const StreamLite &stream, DmaOp dmaOp,
                            u32 taskId);

    void ReduceProfilingProcess(const RmaBufSliceLite &loc, const RmtRmaBufSliceLite &rmt, const ReduceIn &reduceIn,
                                      const StreamLite &stream, u32 taskId);

    void ParseLocNotifyVec(std::vector<char> &data);

    void ParseRmtBufferVec(std::vector<char> &data, RmtUbBufLiteVec &vec, RmaUbBufType rmtType) const;
 
    void ParseLocBufferVec(std::vector<char> &data, LocUbBufLiteVec &vec, RmaUbBufType rmtType) const;

    void ParseConnVec(std::vector<char> &data);

    void BuildUbDbSendTask(const StreamLite &stream, const UbJettyLiteId &jettyLiteId, u32 pi);

    void BuildNotifyWaitTask(const StreamLite &stream, u32 notifyId);

    void CheckConnVec(const std::string &desc);

    void SetFenceConfig(SqeConfigLite &cfg);
};

} // namespace Hccl
#endif