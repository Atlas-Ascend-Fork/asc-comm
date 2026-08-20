/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file hccl_common.h
 * \brief asccomm common definitions
 */
#ifndef INCLUDE_ADV_API_HCOMM_HCOMM_COMMON_H
#define INCLUDE_ADV_API_HCOMM_HCOMM_COMMON_H

#include <cstdint>

namespace AscendC {
using channel_handle = uint64_t;

/**
 * @brief 通信协议类型枚举
 */
typedef enum {
    comm_protocol_reserved = -1, ///< 保留协议类型
    comm_protocol_hccs = 0,      ///< HCCS协议
    comm_protocol_roce = 1,      ///< RDMA over Converged Ethernet
    comm_protocol_pcie = 2,      ///< PCIE协议
    comm_protocol_sio = 3,       ///< SIO协议
    comm_protocol_ubc_ctp = 4,   ///< 华为统一总线UBC_CTP
    comm_protocol_ubc_tp = 5,    ///< 华为统一总线UBC_TP
    comm_protocol_ub_mem = 6,    ///< UB_MEM
    comm_protocol_uboe = 7,      ///< UBoE
    comm_protocol_hccs_only = 8, ///< 一卡双DIE使用HCCS
} comm_protocol;

enum class asccomm_urma_reduce_op : uint32_t {
    max = 0x8U,
    min = 0x9U,
    sum = 0xAU,
};

} // namespace AscendC

#endif // INCLUDE_ADV_API_HCOMM_HCOMM_COMMON_H
