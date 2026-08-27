/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_UB_CONN_LITE_H
#define ASCCOMM_UB_CONN_LITE_H

#include <functional>
#include "asccomm_backend_types.h"
#include "../buffer/rma_buf_slice_lite.h"
#include "../buffer/rmt_rma_buf_slice_lite.h"
#include "udma_data_struct.h"

namespace Asc {

enum class SlicePosition { ONLY = 0, FIRST = 1, MIDDLE = 2, LAST = 3 };
struct UbConnLiteParam {
    u32 dieId;
    u32 funcId;
    u32 jettyId;

    u64  sqVa;
    u32  sqDepth;
    u32  tpn;
    u8 rmtEid[16];

    u32 maxReadSize;
    u32 maxWriteSize;
};

class UbConnLite {
public:
    void import_context(const UbConnLiteParam &liteParam);

    void import_queue_state(u16 queuePi);
    void export_queue_state(u16 &queuePi) const;

    void read(const RmaBufSliceLite &loc, const RmtRmaBufSliceLite &rmt, const SqeConfigLite &cfg,
                 ConnLiteOperationOut &out);
    void read_reduce(ReduceIn reduceIn, const RmaBufSliceLite &loc, const RmtRmaBufSliceLite &rmt,
                       const SqeConfigLite &cfg, ConnLiteOperationOut &out);
    void write(const RmaBufSliceLite &loc, const RmtRmaBufSliceLite &rmt, const SqeConfigLite &cfg,
                  ConnLiteOperationOut &out);
    void inline_write(const u8 *data, u16 size, const RmtRmaBufSliceLite &rmt, const SqeConfigLite &cfg,
                        ConnLiteOperationOut &out);
    void write_reduce(DataType dataType, ReduceOp reduceOp, const RmaBufSliceLite &loc,
                        const RmtRmaBufSliceLite &rmt, const SqeConfigLite &cfg, ConnLiteOperationOut &out);
    void write_with_notify(const RmaBufSliceLite &loc, const RmtRmaBufSliceLite &rmt,
                            const SqeConfigLite &cfg, ConnLiteOperationOut &out,
                            const RmtRmaBufSliceLite &notify, u64 notifyData);
    void write_reduce_with_notify(DataType dataType, ReduceOp reduceOp, const RmaBufSliceLite &loc,
                                  const RmtRmaBufSliceLite &rmt, const SqeConfigLite &cfg,
                                  ConnLiteOperationOut &out, const RmtRmaBufSliceLite &notify, u64 notifyData);

    void fill_comm_sqe(UdmaSqeCommon *sqe, const RmtRmaBufSliceLite &rmt, const SqeConfigLite &cfg, u32 opCode,
                     SlicePosition slicePos = SlicePosition::ONLY);

    void fill_notify_sqe(struct UdmaSqeNotify *sqe, const RmtRmaBufSliceLite &notify, u64 notifyData) const;
    void fill_local_sge_sqe(UdmaNormalSge *sqe, const RmaBufSliceLite &loc) const;

private:
    u16  pi{0};
    u32  maxReadSize{0};
    u32  maxWriteSize{0};
    u32  dieId_{0};
    u32  funcId_{0};
    u32  jettyId_{0};
    u64  sqVa_{0};
    u32  sqDepth_{0};
    u32  tpn_{0};
    u8   rmtEid_[16]{};
    void process_slices(const RmaBufSliceLite &loc, const RmtRmaBufSliceLite &rmt, u32 maxSliceSize,
        std::function<void(const RmaBufSliceLite &, const RmtRmaBufSliceLite &, SlicePosition)> processOneSlice,
        DataType dataType = DataType::INVALID) const;
    void process_slices_with_notify(
        const RmaBufSliceLite &loc, const RmtRmaBufSliceLite &rmt, u32 maxSliceSize,
        std::function<void(const RmaBufSliceLite &, const RmtRmaBufSliceLite &, SlicePosition)> processOneSlice,
        std::function<void(const RmaBufSliceLite &, const RmtRmaBufSliceLite &, SlicePosition)> processOneSliceWithNotify,
        DataType                                                                 dataType = DataType::INVALID) const;
    void process_one_wqe(UdmaSqeWrite *sqe, UdmaSqOpcode opCode);
    void process_one_wqe_with_notify(const RmaBufSliceLite &loc, const RmtRmaBufSliceLite &rmt, const SqeConfigLite &cfg,
                                 UdmaSqeWriteWithNotify *sqe, const RmtRmaBufSliceLite &notify, u64 notifyData,
                                 u32 opCode, SlicePosition slicePos);
    void fill_comm_sqe_reduce_info(UdmaSqeCommon &sqeComm, ReduceOp reduceOp, DataType dataType, u32 udfType = 0) const;
    void fill_one_sqe_write(const RmaBufSliceLite &loc, const RmtRmaBufSliceLite &rmt, const SqeConfigLite &cfg,
                         UdmaSqeWrite *sqe, UdmaSqOpcode opCode, SlicePosition slicePos);
    void memory_set_and_copy(u8 *va, u32 sqeSize, void *sqe);
};
} // namespace Asc

#endif // ASCCOMM_UB_CONN_LITE_H
