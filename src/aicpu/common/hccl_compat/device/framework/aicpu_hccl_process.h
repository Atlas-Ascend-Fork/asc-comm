/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

// [hccl_compat] A5 AICPU 数据面兼容薄头。
//
// AicpuHcclProcess / hccl::HcclCommAicpu 属 hcomm 控制面（非 A5 / A2·A3 通信域管理），
// 保留在 hcomm 仓，不随 A5 数据面迁移。数据面适配层
// （aicpu_ts_primitives_c_adpt.cc）的 HcommAcquireComm / HcommReleaseComm 等控制面函数
// 中含 `if (deviceType != DEV_TYPE_950)` 的非 A5 分支会引用它们——该分支在 A5（Ascend 950）
// 运行期不执行，但需编译通过。故此处仅声明适配层实际用到的接口，实现由 hcomm 控制面在
// 链接期提供。原始完整定义见 hcomm 仓
// src/legacy/ascend910/framework/device/framework/aicpu_hccl_process.h
// 及 aicpu_communicator.h（HcclCommAicpu）。

#ifndef AICPU_HCCL_PROCESS_H
#define AICPU_HCCL_PROCESS_H

#include <string>
#include "hccl/base.h"   // HcclResult

namespace hccl {
// 控制面通信域对象。数据面仅在非 A5 分支上取指针并调用 SetDispatcherCtxOnThread()，
// 此处只声明该成员，完整定义（成员数据等）保留在 hcomm 控制面。
class HcclCommAicpu {
public:
    HcclResult SetDispatcherCtxOnThread();
};
}  // namespace hccl

class AicpuHcclProcess {
public:
    // 数据面适配层实际引用到的接口子集：
    static hccl::HcclCommAicpu *AicpuGetCommbyGroup(const std::string &group);
    static void AicpuReleaseCommbyGroup(const std::string &group);
};

#endif // AICPU_HCCL_PROCESS_H
