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
 * \brief Hcomm common definitions
 */
#ifndef INCLUDE_ADV_API_HCOMM_HCOMM_COMMON_H
#define INCLUDE_ADV_API_HCOMM_HCOMM_COMMON_H

#include <cstdint>

namespace AscendC {
using ChannelHandle = uint64_t;

/**
 * @brief 通信协议类型枚举
 */
typedef enum {
    COMM_PROTOCOL_RESERVED = -1,  ///< 保留协议类型
    COMM_PROTOCOL_HCCS = 0,       ///< HCCS协议
    COMM_PROTOCOL_ROCE = 1,       ///< RDMA over Converged Ethernet
    COMM_PROTOCOL_PCIE = 2,       ///< PCIE协议
    COMM_PROTOCOL_SIO = 3,        ///< SIO协议
    COMM_PROTOCOL_UBC_CTP = 4,    ///< 华为统一总线UBC_CTP
    COMM_PROTOCOL_UBC_TP = 5,     ///< 华为统一总线UBC_TP
    COMM_PROTOCOL_UB_MEM = 6,     ///< UB_MEM
    COMM_PROTOCOL_UBOE = 7,       ///< UBoE
    COMM_PROTOCOL_HCCS_ONLY = 8,  ///< 一卡双DIE使用HCCS
} CommProtocol;

} // namespace AscendC

#endif // LIB_HCOMM_HCOMM_COMMON_H
