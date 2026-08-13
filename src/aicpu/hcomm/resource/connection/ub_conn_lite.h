/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_POD_UB_CONN_LITE_H
#define HCOMM_POD_UB_CONN_LITE_H

#include <functional>
#include "hcomm_pod_backend_types.h"
#include "../buffer/rma_buf_slice_lite.h"
#include "../buffer/rmt_rma_buf_slice_lite.h"
#include "udma_data_struct.h"

namespace Hccl {

enum class SlicePosition { ONLY = 0, FIRST = 1, MIDDLE = 2, LAST = 3 };
struct PodUbConnLiteParam {
    u32 dieId;
    u32 funcId;
    u32 jettyId;

    u64  dbAddr;
    u64  sqVa;
    u32  sqDepth;
    u32  tpn;
    bool dwqeCacheLocked;
    u32  jfcPollMode; // 0代表STARS POLL， 1代表软件Poll
    u64  sqCiAddr;    // 预留给 软件poll CQ 的Jetty使用

    u8 rmtEid[16];
    u8 locEid[16];

    u32 maxReadSize;
    u32 maxWriteSize;

};

class PodUbConnLite {
public:
    void ImportPodContext(const PodUbConnLiteParam &liteParam);

    void ImportQueueState(u16 queuePi, u16 queueCi, u32 queuePiDetourCount, u32 queueCiDetourCount);
    void ExportQueueState(u16 &queuePi, u16 &queueCi, u32 &queuePiDetourCount, u32 &queueCiDetourCount) const;

    void ReadPod(const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg,
                 PodConnLiteOperationOut &out);
    void ReadReducePod(PodReduceIn reduceIn, const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt,
                       const PodSqeConfigLite &cfg, PodConnLiteOperationOut &out);
    void WritePod(const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg,
                  PodConnLiteOperationOut &out);
    void InlineWritePod(const u8 *data, u16 size, const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg,
                        PodConnLiteOperationOut &out);
    void WriteReducePod(PodDataType dataType, PodReduceOp reduceOp, const PodRmaBufSliceLite &loc,
                        const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg, PodConnLiteOperationOut &out);
    void WriteWithNotifyPod(const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt,
                            const PodSqeConfigLite &cfg, PodConnLiteOperationOut &out,
                            const PodRmtRmaBufSliceLite &notify, u64 notifyData);
    void WriteReduceWithNotifyPod(PodDataType dataType, PodReduceOp reduceOp, const PodRmaBufSliceLite &loc,
                                  const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg,
                                  PodConnLiteOperationOut &out, const PodRmtRmaBufSliceLite &notify, u64 notifyData);

    void FillCommSqe(PodUdmaSqeCommon *sqe, const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg, u32 opCode,
                     SlicePosition slicePos = SlicePosition::ONLY);

    void FillNotifySqe(struct PodUdmaSqeNotify *sqe, const PodRmtRmaBufSliceLite &notify, u64 notifyData) const;
    void FillLocalSgeSqe(PodUdmaNormalSge *sqe, const PodRmaBufSliceLite &loc) const;

private:
    u16  pi{0};
    u16  ci{0};
    u32  piDetourCount{0};
    u32  ciDetourCount{0};
    u32  maxReadSize{0};
    u32  maxWriteSize{0};
    u32  dieId_{0};
    u32  funcId_{0};
    u32  jettyId_{0};
    u64  dbAddr_{0};
    u64  sqVa_{0};
    u32  sqDepth_{0};
    bool dwqeCacheLocked_{false};
    u32  jfcPollMode_{0};
    u32  tpn_{0};
    u8   rmtEid_[16]{};
    u8   locEid_[16]{};
    void ProcessSlices(const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt, u32 maxSliceSize,
        std::function<void(const PodRmaBufSliceLite &, const PodRmtRmaBufSliceLite &, SlicePosition)> processOneSlice,
        PodDataType dataType = PodDataType::INVALID) const;
    void ProcessSlicesWithNotify(
        const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt, u32 maxSliceSize,
        std::function<void(const PodRmaBufSliceLite &, const PodRmtRmaBufSliceLite &, SlicePosition)> processOneSlice,
        std::function<void(const PodRmaBufSliceLite &, const PodRmtRmaBufSliceLite &, SlicePosition)> processOneSliceWithNotify,
        PodDataType                                                                 dataType = PodDataType::INVALID) const;
    void ProcessOneWqe(PodUdmaSqeWrite *sqe, PodUdmaSqOpcode opCode);
    void ProcessOneWqeWithNotify(const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg,
                                 PodUdmaSqeWriteWithNotify *sqe, const PodRmtRmaBufSliceLite &notify, u64 notifyData,
                                 u32 opCode, SlicePosition slicePos);
    void FillCommSqeReduceInfo(PodUdmaSqeCommon &sqeComm, PodReduceOp reduceOp, PodDataType dataType, u32 udfType = 0) const;
    void FillOneSqeWrite(const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg,
                         PodUdmaSqeWrite *sqe, PodUdmaSqOpcode opCode, SlicePosition slicePos);
    void MemorySetAndCopy(u8 *va, u32 sqeSize, void *sqe);
};
} // namespace Hccl

#endif // HCOMM_POD_UB_CONN_LITE_H
