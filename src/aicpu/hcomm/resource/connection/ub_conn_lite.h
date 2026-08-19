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
#include "asccomm_backend_types.h"
#include "../buffer/rma_buf_slice_lite.h"
#include "../buffer/rmt_rma_buf_slice_lite.h"
#include "udma_data_struct.h"

namespace Asc {

enum class SlicePosition { ONLY = 0, FIRST = 1, MIDDLE = 2, LAST = 3 };
struct PodUbConnLiteParam {
    u32 dieId;
    u32 funcId;
    u32 jettyId;

    u64  sqVa;
    u32  sqDepth;
    u32  tpn;
    bool dwqeCacheLocked;
    u8 rmtEid[16];

    u32 maxReadSize;
    u32 maxWriteSize;

};

class PodUbConnLite {
public:
    void import_pod_context(const PodUbConnLiteParam &liteParam);

    void import_queue_state(u16 queuePi, u32 queuePiDetourCount);
    void export_queue_state(u16 &queuePi, u32 &queuePiDetourCount) const;

    void read_pod(const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg,
                 PodConnLiteOperationOut &out);
    void read_reduce_pod(PodReduceIn reduceIn, const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt,
                       const PodSqeConfigLite &cfg, PodConnLiteOperationOut &out);
    void write_pod(const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg,
                  PodConnLiteOperationOut &out);
    void inline_write_pod(const u8 *data, u16 size, const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg,
                        PodConnLiteOperationOut &out);
    void write_reduce_pod(PodDataType dataType, PodReduceOp reduceOp, const PodRmaBufSliceLite &loc,
                        const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg, PodConnLiteOperationOut &out);
    void write_with_notify_pod(const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt,
                            const PodSqeConfigLite &cfg, PodConnLiteOperationOut &out,
                            const PodRmtRmaBufSliceLite &notify, u64 notifyData);
    void write_reduce_with_notify_pod(PodDataType dataType, PodReduceOp reduceOp, const PodRmaBufSliceLite &loc,
                                  const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg,
                                  PodConnLiteOperationOut &out, const PodRmtRmaBufSliceLite &notify, u64 notifyData);

    void fill_comm_sqe(PodUdmaSqeCommon *sqe, const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg, u32 opCode,
                     SlicePosition slicePos = SlicePosition::ONLY);

    void fill_notify_sqe(struct PodUdmaSqeNotify *sqe, const PodRmtRmaBufSliceLite &notify, u64 notifyData) const;
    void fill_local_sge_sqe(PodUdmaNormalSge *sqe, const PodRmaBufSliceLite &loc) const;

private:
    u16  pi{0};
    u32  piDetourCount{0};
    u32  maxReadSize{0};
    u32  maxWriteSize{0};
    u32  dieId_{0};
    u32  funcId_{0};
    u32  jettyId_{0};
    u64  sqVa_{0};
    u32  sqDepth_{0};
    bool dwqeCacheLocked_{false};
    u32  tpn_{0};
    u8   rmtEid_[16]{};
    void process_slices(const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt, u32 maxSliceSize,
        std::function<void(const PodRmaBufSliceLite &, const PodRmtRmaBufSliceLite &, SlicePosition)> processOneSlice,
        PodDataType dataType = PodDataType::INVALID) const;
    void process_slices_with_notify(
        const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt, u32 maxSliceSize,
        std::function<void(const PodRmaBufSliceLite &, const PodRmtRmaBufSliceLite &, SlicePosition)> processOneSlice,
        std::function<void(const PodRmaBufSliceLite &, const PodRmtRmaBufSliceLite &, SlicePosition)> processOneSliceWithNotify,
        PodDataType                                                                 dataType = PodDataType::INVALID) const;
    void process_one_wqe(PodUdmaSqeWrite *sqe, PodUdmaSqOpcode opCode);
    void process_one_wqe_with_notify(const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg,
                                 PodUdmaSqeWriteWithNotify *sqe, const PodRmtRmaBufSliceLite &notify, u64 notifyData,
                                 u32 opCode, SlicePosition slicePos);
    void fill_comm_sqe_reduce_info(PodUdmaSqeCommon &sqeComm, PodReduceOp reduceOp, PodDataType dataType, u32 udfType = 0) const;
    void fill_one_sqe_write(const PodRmaBufSliceLite &loc, const PodRmtRmaBufSliceLite &rmt, const PodSqeConfigLite &cfg,
                         PodUdmaSqeWrite *sqe, PodUdmaSqOpcode opCode, SlicePosition slicePos);
    void memory_set_and_copy(u8 *va, u32 sqeSize, void *sqe);
};
} // namespace Asc

#endif // HCOMM_POD_UB_CONN_LITE_H
