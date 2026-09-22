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

#include "acl/acl_rt.h"

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

// 桩：ccu_device_context.cc 经 runtime 取当前线程设备逻辑 id，UT 环境无真实 runtime，固定成功并返回设备 0
extern "C" aclError aclrtGetDevice(int32_t* deviceId)
{
    if (deviceId == nullptr) {
        return 1; // 非 0 即失败，与 ACL_SUCCESS 语义一致
    }
    *deviceId = 0;
    return ACL_SUCCESS;
}
