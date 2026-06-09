/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#ifndef HCCL_COMM_PROFILING_LITE_H
#define HCCL_COMM_PROFILING_LITE_H
#include "mirror_task_manager_lite.h"
#include "profiling_reporter_lite.h"
#include "types.h"

namespace hccl {

class HcclCommProfilingLite {
public:
    // 构造函数
    HcclCommProfilingLite(Hccl::DevId deviceId, Hccl::MirrorTaskManagerLite* mirrorTaskManagerLite);
    
    // 上报所有任务
    void ReportAllTasks();

    // 更新Profiling统计
    void UpdateProfStat();
    
    // 获取MirrorTaskManager
    Hccl::MirrorTaskManagerLite* GetMirrorTaskManagerLite() const;
    
private:
    std::unique_ptr<Hccl::MirrorTaskManagerLite> mirrorTaskManagerLite_;
    std::unique_ptr<Hccl::ProfilingReporterLite> profilingReporterLite_;
};
}

#endif 