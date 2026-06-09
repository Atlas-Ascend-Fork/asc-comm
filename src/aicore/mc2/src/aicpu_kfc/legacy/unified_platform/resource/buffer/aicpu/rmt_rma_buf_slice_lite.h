/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#ifndef HCCLV2_RMT_RMA_BUF_SLICE_LITE_H
#define HCCLV2_RMT_RMA_BUF_SLICE_LITE_H
#include "hccl/base.h"

namespace Hccl {
class RmtRmaBufSliceLite {
public:
    RmtRmaBufSliceLite(u64 addr, u64 size, u32 rkey, u32 tokenId, u32 tokenValue);

    inline u64 GetAddr() const
    {
        return addr_;
    }

    inline u64 GetSize() const
    {
        return size_;
    }

    inline u32 GetRkey() const
    {
        return rkey_;
    }

    inline u32 GetTokenId() const
    {
        return tokenId_;
    }

    inline u32 GetTokenValue() const
    {
        return tokenValue_;
    }

    std::string Describe() const;

private:
    u64 addr_;
    u64 size_;
    u32 rkey_;
    u32 tokenId_;
    u32 tokenValue_;
};
} // namespace Hccl
#endif
