/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef ASCCOMM_UB_JETTY_ID_LITE_H
#define ASCCOMM_UB_JETTY_ID_LITE_H

#include <stdint.h>

namespace Asc {

struct UbJettyLiteId {
    uint32_t dieId_;
    uint32_t funcId_;
    uint32_t jettyId_;

    uint32_t get_die_id() const { return dieId_; }

    uint32_t get_func_id() const { return funcId_; }

    uint32_t get_jetty_id() const { return jettyId_; }

    UbJettyLiteId(uint32_t dieId, uint32_t funcId, uint32_t jettyId) : dieId_(dieId), funcId_(funcId), jettyId_(jettyId)
    {}
};

struct UbJettyLiteAttr {
    const uint64_t dbAddr_;
    const uint64_t sqVa_;
    const uint32_t sqDepth_;
    const uint32_t tpn_;
    const bool dwqeCacheLocked_;
    const uint32_t jfcPollMode_; // 0代表STARS POLL， 1代表软件Poll
    const uint64_t sqCiAddr_;    // 预留给 软件poll CQ 的Jetty使用
    UbJettyLiteAttr(
        uint64_t dbAddr, uint64_t sqVa, uint32_t sqDepth, uint32_t tpn, bool dwqeCacheLocked = false,
        uint32_t jfcPollMode = 0, uint64_t sqCiAddr = 0)
        : dbAddr_(dbAddr),
          sqVa_(sqVa),
          sqDepth_(sqDepth),
          tpn_(tpn),
          dwqeCacheLocked_(dwqeCacheLocked),
          jfcPollMode_(jfcPollMode),
          sqCiAddr_(sqCiAddr)
    {}
};

} // namespace Asc
#endif
