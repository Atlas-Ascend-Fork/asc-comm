/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/common/ccu_dev_mem.h"

#include "acl/acl_rt.h"
#include "base/log_types.h"

#include "hcomm/common/ccu_log.h"

namespace asc {
namespace ccu_rep {

ccu_dev_mem::ccu_dev_mem(std::size_t size)
{
    if (size == 0) {
        HCCL_ERROR("[CcuDevMem] alloc size should not be 0.");
        return;
    }

    // 与 CCU 数据面既有行为保持一致：申请高带宽内存并标记归属模块，便于内存占用统计
    aclrtMallocAttrValue module_id_value{};
    module_id_value.moduleId = static_cast<uint16_t>(HCCL);
    aclrtMallocAttribute attrs{};
    attrs.attr = ACL_RT_MEM_ATTR_MODULE_ID;
    attrs.value = module_id_value;
    aclrtMallocConfig cfg{};
    cfg.attrs = &attrs;
    cfg.numAttrs = 1;

    void* dev_ptr = nullptr;
    aclError ret =
        aclrtMallocWithCfg(&dev_ptr, size, static_cast<aclrtMemMallocPolicy>(ACL_MEM_TYPE_HIGH_BAND_WIDTH), &cfg);
    if (ret != ACL_SUCCESS || dev_ptr == nullptr) {
        HCCL_ERROR("[CcuDevMem] aclrtMallocWithCfg failed, ret[%d] size[%zu].", ret, size);
        return;
    }

    addr_ = reinterpret_cast<uint64_t>(dev_ptr);
    size_ = size;
}

ccu_dev_mem::~ccu_dev_mem()
{
    if (addr_ == 0) {
        return;
    }
    // 析构不抛异常：释放失败只记录日志
    aclError ret = aclrtFree(reinterpret_cast<void*>(addr_));
    if (ret != ACL_SUCCESS) {
        HCCL_ERROR("[CcuDevMem] aclrtFree failed, ret[%d].", ret);
    }
    addr_ = 0;
    size_ = 0;
}

} // namespace ccu_rep
} // namespace asc
