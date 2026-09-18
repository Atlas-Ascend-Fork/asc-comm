/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @brief 声明 Mesh1D Mem2Mem AllGather 的 Host 侧入口。
 *
 * 该接口接收 Device Buffer、单 Rank 元素数量、HCCL 通信域及 CCU 任务下发所用
 * Stream。
 */
#ifndef ASC_COMM_EXAMPLE_CCU_ALLGATHER_MESH1D_MEM2MEM_EXEC_OP_H
#define ASC_COMM_EXAMPLE_CCU_ALLGATHER_MESH1D_MEM2MEM_EXEC_OP_H

#include <cstdint>

#include <acl/acl_rt.h>
#include <hcomm/hcomm_types.h>

namespace CcuAgMem2mem {

HcclResult AllGatherMesh1D(
    void* sendBuf, void* recvBuf, uint64_t count_, uint32_t rankId, uint32_t rankSize, HcclComm comm,
    aclrtStream stream);

} // namespace CcuAgMem2mem

#endif // ASC_COMM_EXAMPLE_CCU_ALLGATHER_MESH1D_MEM2MEM_EXEC_OP_H
