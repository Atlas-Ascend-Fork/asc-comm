/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "asccomm/asccomm.h"
#include "kernel_operator.h"

#include "asccomm_batch_write_def.h"

// 设备侧多通道批量写：多通道batch handle绑定一块UB工作区，逐通道get_handle_ref后添加
// write_nbi，一次batch_commit统一提交、drain等待全部完成。失败经context上报给host判定。
namespace asccomm_batch_write_example {

class kernel_asccomm_batch_write {
    using asccomm_type = asc::asccomm<asc::COMM_PROTOCOL_UB_CTP>;

public:
    // 从context取多通道句柄与本端buffer地址，批量工作区由入口UB数组提供。
    __aicore__ inline void init(GM_ADDR context, __ubuf__ uint8_t* workspace)
    {
        this->context = reinterpret_cast<__gm__ batch_context*>(context);
        this->local_buffer = reinterpret_cast<GM_ADDR>(this->context->local_buffer);
        this->batch_buf = workspace;
    }

    __aicore__ inline void process()
    {
        context->result = KERNEL_SUCCESS;
        context->asccomm_result = asc::HCOMM_SUCCESS;
        // 批量接口无需asccomm_init：UB工作区由make_batch_handle绑定；
        // 多通道句柄用于向多个对端批量提交，get_handle_ref切换逻辑通道与远端内存。
        auto multi_batch_handle = asccomm.make_batch_handle(
            static_cast<asc::multi_channel_handle>(context->multi_channel), batch_buf, BATCH_BUFFER_SIZE);

        for (uint32_t channel_index = 0U; channel_index < context->channel_num; ++channel_index) {
            GM_ADDR remote_addr = reinterpret_cast<GM_ADDR>(context->remote_buffers[channel_index]) + RECV_BASE_OFFSET;
            // 多通道共享同一batch handle与工作区，get_handle_ref切换当前逻辑通道与远端内存。
            auto& peer_batch_handle = asccomm.get_handle_ref(multi_batch_handle, channel_index, remote_addr);
            context->asccomm_result =
                asccomm.write_nbi(peer_batch_handle, remote_addr, local_buffer + SEND_OFFSET, DATA_SIZE);
            if (context->asccomm_result != asc::HCOMM_SUCCESS) {
                context->result = KERNEL_WRITE_FAILED;
                return;
            }
        }
        // 统一提交当前批次；提交成功后句柄与工作区可复用提交下一批。
        context->asccomm_result = asccomm.batch_commit(multi_batch_handle);
        if (context->asccomm_result != asc::HCOMM_SUCCESS) {
            context->result = KERNEL_COMMIT_FAILED;
            return;
        }
        // 等待该批量句柄提交的全部任务完成。
        context->asccomm_result = asccomm.drain(multi_batch_handle);
        if (context->asccomm_result != asc::HCOMM_SUCCESS) {
            context->result = KERNEL_DRAIN_FAILED;
        }
    }

private:
    __ubuf__ uint8_t* batch_buf{nullptr};
    asccomm_type asccomm;
    __gm__ batch_context* context{nullptr};
    GM_ADDR local_buffer{nullptr};
};

} // namespace asccomm_batch_write_example

// 批量写kernel入口：多通道批量下发。
extern "C" __vector__ __global__ __aicore__ void asccomm_batch_write_kernel(GM_ADDR context)
{
    // 批量工作区
    __ubuf__ uint8_t batch_buf[asccomm_batch_write_example::BATCH_BUFFER_SIZE];
    asccomm_batch_write_example::kernel_asccomm_batch_write op;
    op.init(context, batch_buf);
    op.process();
}
