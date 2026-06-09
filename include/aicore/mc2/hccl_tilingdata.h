/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/

/*!
* \file hccl_tilingdata.h
* \brief
*/
#ifndef LIB_HCCL_HCCL_TILINGDATA_H
#define LIB_HCCL_HCCL_TILINGDATA_H

#include "register/tilingdata_base.h"

namespace optiling {
/**
 * @deprecated The Mc2ServerCfg structure will no longer be supported in subsequent versions, so please do not use it.
 */
BEGIN_TILING_DATA_DEF(Mc2ServerCfg)
TILING_DATA_FIELD_DEF(uint32_t, version);
TILING_DATA_FIELD_DEF(uint8_t, debugMode);
TILING_DATA_FIELD_DEF(uint8_t, sendArgIndex);
TILING_DATA_FIELD_DEF(uint8_t, recvArgIndex);
TILING_DATA_FIELD_DEF(uint8_t, commOutArgIndex);
TILING_DATA_FIELD_DEF_ARR(uint8_t, 8, reserved);
END_TILING_DATA_DEF; // 16 bytes
REGISTER_TILING_DATA_CLASS(Mc2ServerCfgOpApi, Mc2ServerCfg)

/**
 * @deprecated The Mc2HcommCfg structure will no longer be supported in subsequent versions, so please do not use it.
 */
BEGIN_TILING_DATA_DEF(Mc2HcommCfg)
TILING_DATA_FIELD_DEF(uint8_t, skipLocalRankCopy);
TILING_DATA_FIELD_DEF(uint8_t, skipBufferWindowCopy);
TILING_DATA_FIELD_DEF(uint8_t, stepSize);
TILING_DATA_FIELD_DEF_ARR(char, 13, reserved);
TILING_DATA_FIELD_DEF_ARR(char, 128, groupName);
TILING_DATA_FIELD_DEF_ARR(char, 128, algConfig);
TILING_DATA_FIELD_DEF(uint32_t, opType);
TILING_DATA_FIELD_DEF(uint32_t, reduceType);
END_TILING_DATA_DEF; // 280 bytes
REGISTER_TILING_DATA_CLASS(Mc2HcommCfgOpApi, Mc2HcommCfg)

BEGIN_TILING_DATA_DEF(Mc2InitTiling)
TILING_DATA_FIELD_DEF_ARR(uint8_t, 64, reserved);
END_TILING_DATA_DEF; // 64 bytes
REGISTER_TILING_DATA_CLASS(Mc2InitTilingOpApi, Mc2InitTiling)

BEGIN_TILING_DATA_DEF(Mc2CcTiling)
TILING_DATA_FIELD_DEF_ARR(uint8_t, 512, reserved);
END_TILING_DATA_DEF; // 512 bytes
REGISTER_TILING_DATA_CLASS(Mc2CcTilingOpApi, Mc2CcTiling)
} // namespace optiling
#endif