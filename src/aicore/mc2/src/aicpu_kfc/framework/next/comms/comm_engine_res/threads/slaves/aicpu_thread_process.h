/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#ifndef __AICPU_THREAD_PROCESS_H__
#define __AICPU_THREAD_PROCESS_H__

#include "common.h"
#include "aicpu_launch_manager.h"
#include "aicpu_ts_thread.h"

class AicpuThreadProcess {
public:
    ~AicpuThreadProcess() = default;
    static HcclResult InitThreads(ThreadMgrAicpuParam *param);
    static HcclResult AicpuThreadInit(ThreadMgrAicpuParam *param);
    static HcclResult AicpuThreadDestroy(ThreadMgrAicpuParam *param);
    static HcclResult AicpuThreadSupplementNotify(ThreadMgrAicpuParam *param);
private:
    static HcclResult ResumeThread(ThreadMgrAicpuParam *param,
        std::vector<std::shared_ptr<hccl::Thread>> &outThreads, bool isSupplementNotify);
    static std::mutex mutex_;
    static std::vector<std::shared_ptr<hccl::Thread>> threads_;
};
#endif // __AICPU_THREAD_PROCESS_H__