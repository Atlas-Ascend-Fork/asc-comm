/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef OPS_HCCL_ALLTOALLV_CUSTOM_ALLTOALLV_H
#define OPS_HCCL_ALLTOALLV_CUSTOM_ALLTOALLV_H

#include <acl/acl.h>
#include <hccl/hccl_comm.h>
#include <hccl/hccl_res.h>
#include <hccl/hccl_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 自定义 AlltoAllV 算子 */
HcclResult AlltoAllVCustom(
    const void* sendBuf, const uint64_t* sendCounts, const uint64_t* sendDispls, HcclDataType sendType, void* recvBuf,
    const uint64_t* recvCounts, const uint64_t* recvDispls, HcclDataType recvType, HcclComm comm, aclrtStream stream);

#ifdef __cplusplus
}
#endif

#endif // OPS_HCCL_ALLTOALLV_CUSTOM_ALLTOALLV_H
