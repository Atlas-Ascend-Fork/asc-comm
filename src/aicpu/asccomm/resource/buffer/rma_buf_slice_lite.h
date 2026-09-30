/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef ASCCOMM_RMA_BUFFER_SLICE_LITE_H
#define ASCCOMM_RMA_BUFFER_SLICE_LITE_H

#include <stdint.h>

namespace Asc {
class RmaBufSliceLite {
public:
    constexpr RmaBufSliceLite(uint64_t addr, uint64_t size, uint32_t lkey, uint32_t tokenId)
        : addr_(addr), size_(size), lkey_(lkey), tokenId_(tokenId)
    {}

    inline uint64_t get_addr() const { return addr_; }

    inline uint64_t get_size() const { return size_; }

    inline uint32_t get_lkey() const { return lkey_; }

    inline uint32_t get_token_id() const { return tokenId_; }

private:
    uint64_t addr_;
    uint64_t size_;
    uint32_t lkey_;
    uint32_t tokenId_;
};
} // namespace Asc
#endif
