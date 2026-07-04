/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * See LICENSE in the root of the software repository for the full text of the License.
 */

// [hccl_compat] A5 AICPU 数据面兼容薄头。
// CollCommAicpuMgr 属 hcomm 控制面（通信域管理），保留在 hcomm 仓。数据面适配层仅将其
// 作为不透明指针使用（AicpuGetCommMgrbyGroup 返回值 + 空判，无成员访问），故此处仅前向
// 声明。原始完整定义见 hcomm 仓
// src/coll_communicator_mgr/communicator/device/coll_comm_aicpu_mgr.h。

#ifndef COLL_COMM_AICPU_MGR_H
#define COLL_COMM_AICPU_MGR_H

class CollCommAicpuMgr;

#endif // COLL_COMM_AICPU_MGR_H
