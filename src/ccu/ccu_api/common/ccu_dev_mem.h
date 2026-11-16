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

namespace asc {
namespace ccu_rep {

// CCU 数据面自持的 device 内存 RAII 包装。
// 构造与析构只调用公开 ACL runtime C API（aclrtMallocWithCfg / aclrtFree），
// 不再依赖 hcomm 的 Hccl::Buffer / Hccl::DevBuffer，边界上只暴露地址与长度标量。
class ccu_dev_mem {
public:
    // 分配 size 字节 HBM。失败时 GetAddr() 返回 0，由调用方判断并处理。
    explicit ccu_dev_mem(std::size_t size);

    ~ccu_dev_mem();

    ccu_dev_mem(const ccu_dev_mem&) = delete;
    ccu_dev_mem& operator=(const ccu_dev_mem&) = delete;
    ccu_dev_mem(ccu_dev_mem&&) = delete;
    ccu_dev_mem& operator=(ccu_dev_mem&&) = delete;

    uint64_t get_addr() const { return addr_; }

    std::size_t get_size() const { return size_; }

    bool valid() const { return addr_ != 0; }

private:
    uint64_t addr_{0};
    std::size_t size_{0};
};

} // namespace ccu_rep
} // namespace asc

#endif // ASCCOMM_CCU_DEV_MEM_H
