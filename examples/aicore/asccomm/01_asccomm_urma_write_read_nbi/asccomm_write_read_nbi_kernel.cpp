/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdint>

#include "kernel_operator.h"
#include "asccomm_rw_def.h"

namespace asccomm_example {

class kernel_asccomm_write_read {
public:
    // 从context取通道句柄与两端buffer地址，并初始化asccomm的UB工作区。
    __aicore__ inline void init(GM_ADDR context, __ubuf__ uint8_t* workspace)
    {
        this->context = reinterpret_cast<__gm__ comm_context*>(context);
        this->channel = this->context->channel_handle;
        this->local_buffer = reinterpret_cast<GM_ADDR>(this->context->local_buffer_addr);
        this->remote_buffer = reinterpret_cast<GM_ADDR>(this->context->remote_buffer_addr);

        if (asccomm.init(workspace, ASCCOMM_WORKSPACE_SIZE) == asc::HCOMM_SUCCESS) {
            init_ok = true;
        }
    }

    // 按操作类型执行写/读，任一步失败即返回，最后drain等待通道任务全部完成。
    template <uint32_t operation_type>
    __aicore__ inline void process()
    {
        static_assert(
            operation_type == ASCCOMM_READ_ONLY || operation_type == ASCCOMM_WRITE_ONLY ||
                operation_type == ASCCOMM_READ_WRITE,
            "Unsupported Asccomm operation type");
        context->test_result = KERNEL_SUCCESS;
        if (!init_ok) {
            context->test_result = KERNEL_INIT_FAILED;
            return;
        }

        // 下发单边写任务
        if constexpr ((operation_type & ASCCOMM_WRITE_ONLY) != 0U) {
            do_write();
            if (context->test_result != KERNEL_SUCCESS) {
                return;
            }
        }

        // 下发单边读任务
        if constexpr ((operation_type & ASCCOMM_READ_ONLY) != 0U) {
            do_read();
            if (context->test_result != KERNEL_SUCCESS) {
                return;
            }
        }

        // 等待下发的通信任务完成
        if (asccomm.drain(channel) != asc::HCOMM_SUCCESS) {
            context->test_result = KERNEL_DRAIN_FAILED;
        }
    }

private:
    // 单边写：本端发送区 -> 对端写结果区。
    __aicore__ inline void do_write()
    {
        GM_ADDR write_src = local_buffer + SEND_DATA_OFFSET;
        GM_ADDR write_dst = remote_buffer + WRITE_RESULT_OFFSET;
        if (asccomm.write_nbi(channel, write_dst, write_src, DATA_SIZE) != asc::HCOMM_SUCCESS) {
            context->test_result = KERNEL_WRITE_FAILED;
        }
    }

    // 单边读：对端发送区 -> 本端读结果区。
    __aicore__ inline void do_read()
    {
        GM_ADDR read_dst = local_buffer + READ_RESULT_OFFSET;
        GM_ADDR read_src = remote_buffer + SEND_DATA_OFFSET;
        if (asccomm.read_nbi(channel, read_dst, read_src, DATA_SIZE) != asc::HCOMM_SUCCESS) {
            context->test_result = KERNEL_READ_FAILED;
        }
    }

    asc::asccomm<KERNEL_COMM_PROTOCOL> asccomm;
    __gm__ comm_context* context{nullptr};
    asc::ChannelHandle channel{0};
    GM_ADDR local_buffer{nullptr};
    GM_ADDR remote_buffer{nullptr};
    bool init_ok{false};
};

// kernel执行函数：根据operation_type执行对应的通信任务
template <uint32_t operation_type>
__aicore__ inline void run_asccomm_kernel(GM_ADDR context)
{
    __ubuf__ uint8_t workspace[ASCCOMM_WORKSPACE_SIZE];
    kernel_asccomm_write_read op;
    op.init(context, workspace);
    op.process<operation_type>();
}

} // namespace asccomm_example

// 只读kernel入口。
extern "C" __vector__ __global__ __aicore__ void kernel_asccomm_read_nbi(GM_ADDR context)
{
    asccomm_example::run_asccomm_kernel<ASCCOMM_READ_ONLY>(context);
}

// 只写kernel入口。
extern "C" __vector__ __global__ __aicore__ void kernel_asccomm_write_nbi(GM_ADDR context)
{
    asccomm_example::run_asccomm_kernel<ASCCOMM_WRITE_ONLY>(context);
}

// 读写组合kernel入口。
extern "C" __vector__ __global__ __aicore__ void kernel_asccomm_write_read_nbi(GM_ADDR context)
{
    asccomm_example::run_asccomm_kernel<ASCCOMM_READ_WRITE>(context);
}
