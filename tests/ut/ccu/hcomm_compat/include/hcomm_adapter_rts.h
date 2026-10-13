/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_UT_HCOMM_ADAPTER_RTS_H
#define ASCCOMM_CCU_UT_HCOMM_ADAPTER_RTS_H

#include <stdint.h>

#include "hcomm/hcomm_types.h"

enum rtUbDevQueryCmd {
    QUERY_PROCESS_TOKEN = 0,
};

namespace asc {

struct rtMemUbTokenInfo {
    uint64_t va;
    uint64_t size;
    uint32_t token_id;
    uint32_t token_value;
};

HcclResult RtsUbDevQueryInfo(rtUbDevQueryCmd cmd, rtMemUbTokenInfo& devInfo);

} // namespace asc

#endif
