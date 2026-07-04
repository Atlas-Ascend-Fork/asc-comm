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
// 控制面（comm 生命周期管理、独立算子通信域管理、DFX/profiling）保留在 hcomm 仓，
// 不随数据面迁移。数据面适配层（aicpu_ts_primitives_c_adpt.cc）在 HcommAcquireComm /
// HcommReleaseComm / HcommProfilingReport* / HcclDfxRegOpInfoByCommId 等**非数据面原语**
// 路径上引用 AicpuIndopProcess 的少量接口，故此处仅提供这些接口的声明，实现由 hcomm
// 控制面在链接期提供。原始完整定义见 hcomm 仓
// src/coll_communicator_mgr/communicator/device/aicpu_indop_process.h。
//
// 相比原头，此薄头刻意剥离了非 A5 框架依赖（channel_param.h / aicpu_launch_manager.h /
// aicpu_init_param.h / coll_comm_aicpu_mgr.h），CollCommAicpuMgr 仅以前向声明出现——
// 数据面对其只做指针空判，不访问其成员。

#ifndef __AICPU_INDOP_PROCESS_H__
#define __AICPU_INDOP_PROCESS_H__

#include <string>
#include "hccl_diag.h"     // HcclDfxOpInfo

// 控制面类型，数据面仅作不透明指针使用（前向声明即可）
class CollCommAicpuMgr;

class AicpuIndopProcess {
public:
    // 数据面适配层实际引用到的接口子集（其余控制面接口不在此声明）：
    static HcclResult AicpuDfxOpInfoInit(HcclDfxOpInfo *aicpuDfxInfo, const std::string &commTag);
    static CollCommAicpuMgr *AicpuGetCommMgrbyGroup(const std::string &group);
    static void AicpuReleaseCommMgrbyGroup(const std::string &group);
    static HcclResult ProfilingReportDeviceOp(const std::string &group);
    static HcclResult UpdateTask(const std::string &group);
};

#endif // __AICPU_INDOP_PROCESS_H__
