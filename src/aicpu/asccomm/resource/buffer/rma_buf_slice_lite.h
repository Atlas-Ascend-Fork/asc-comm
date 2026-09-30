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

#include "hccl/base.h"

namespace Asc {
class RmaBufSliceLite {
public:
    constexpr RmaBufSliceLite(u64 addr, u64 size, u32 lkey, u32 tokenId)
        : addr_(addr), size_(size), lkey_(lkey), tokenId_(tokenId)
    {}

    inline u64 get_addr() const { return addr_; }

    inline u64 get_size() const { return size_; }

    inline u32 get_lkey() const { return lkey_; }

    inline u32 get_token_id() const { return tokenId_; }

private:
    u64 addr_;
    u64 size_;
    u32 lkey_;
    u32 tokenId_;
};
} // namespace Asc
#endif
