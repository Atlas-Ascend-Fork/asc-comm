/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_DEV_MEM_H
#define ASCCOMM_CCU_DEV_MEM_H

#include <cstddef>
#include <cstdint>

namespace hcomm {
namespace CcuRep {

// CCU 数据面自持的 device 内存 RAII 包装。
// 构造与析构只调用公开 ACL runtime C API（aclrtMallocWithCfg / aclrtFree），
// 不再依赖 hcomm 的 Hccl::Buffer / Hccl::DevBuffer，边界上只暴露地址与长度标量。
class CcuDevMem {
public:
    // 分配 size 字节 HBM。失败时 GetAddr() 返回 0，由调用方判断并处理。
    explicit CcuDevMem(std::size_t size);

    ~CcuDevMem();

    CcuDevMem(const CcuDevMem&) = delete;
    CcuDevMem& operator=(const CcuDevMem&) = delete;
    CcuDevMem(CcuDevMem&&) = delete;
    CcuDevMem& operator=(CcuDevMem&&) = delete;

    uint64_t GetAddr() const { return addr_; }

    std::size_t GetSize() const { return size_; }

    bool Valid() const { return addr_ != 0; }

private:
    uint64_t addr_{0};
    std::size_t size_{0};
};

} // namespace CcuRep
} // namespace hcomm

#endif // ASCCOMM_CCU_DEV_MEM_H
