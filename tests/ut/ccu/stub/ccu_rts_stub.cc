/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm_adapter_rts.h"

namespace asc {

HcclResult RtsUbDevQueryInfo(const rtUbDevQueryCmd cmd, rtMemUbTokenInfo& devInfo)
{
    if (cmd != QUERY_PROCESS_TOKEN) {
        return HcclResult::HCCL_E_PARA;
    }
    devInfo.token_id = 0;
    devInfo.token_value = 0;
    return HcclResult::HCCL_SUCCESS;
}

} // namespace asc

extern "C" int rtUbDevQueryInfo(int cmd, void* devInfo)
{
    if (devInfo == nullptr) {
        return 0;
    }
    return 0;
}
