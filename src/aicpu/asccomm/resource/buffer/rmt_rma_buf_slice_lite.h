/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef ASCCOMM_RMT_RMA_BUF_SLICE_LITE_H
#define ASCCOMM_RMT_RMA_BUF_SLICE_LITE_H

#include <stdint.h>

namespace Asc {
class RmtRmaBufSliceLite {
public:
    constexpr RmtRmaBufSliceLite(
        uint64_t addr, uint64_t size, uint32_t rkey, uint32_t tokenId, uint32_t tokenValue, uint32_t notifyId)
        : addr_(addr), size_(size), rkey_(rkey), tokenId_(tokenId), tokenValue_(tokenValue), notifyId_(notifyId)
    {}

    inline uint64_t get_addr() const { return addr_; }

    inline uint64_t get_size() const { return size_; }

    inline uint32_t get_rkey() const { return rkey_; }

    inline uint32_t get_token_id() const { return tokenId_; }

    inline uint32_t get_token_value() const { return tokenValue_; }

    inline uint32_t get_notify_id() const { return notifyId_; }

private:
    uint64_t addr_;
    uint64_t size_;
    uint32_t rkey_;
    uint32_t tokenId_;
    uint32_t tokenValue_;
    uint32_t notifyId_{UINT32_MAX};
};
} // namespace Asc
#endif
