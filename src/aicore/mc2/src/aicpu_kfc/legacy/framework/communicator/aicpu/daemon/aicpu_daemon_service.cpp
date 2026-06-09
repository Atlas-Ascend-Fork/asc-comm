/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#include "aicpu_daemon_service.h"
#include "sal.h"
#include "log.h"

namespace Hccl {

constexpr u32 TEN_MILLISECOND_OF_USLEEP = 10000;
std::mutex AicpuDaemonService::mutexForFuncs_;

AicpuDaemonService &AicpuDaemonService::GetInstance()
{
    static AicpuDaemonService daemonService;
    return daemonService;
}

void AicpuDaemonService::ServiceRun(void *info)
{
    HCCL_RUN_INFO("Start back ground thread");
    auto commandToBackGroud = static_cast<CommandToBackGroud *>(info);
    while (true) {
        if (*commandToBackGroud == CommandToBackGroud::Stop) {
            HCCL_RUN_INFO("Back ground thread returned");
            break;
        }

        std::unique_lock<std::mutex> lock(mutexForFuncs_);
        for (auto &func : daemonFuncs) {
            func->Call();
            if (needBreak) {
                break;
            }
        }
        lock.unlock();
        
        if (needBreak) {
            HCCL_RUN_INFO("Back ground thread needBreak");
            break;
        }

        SaluSleep(TEN_MILLISECOND_OF_USLEEP);
    }
    HCCL_RUN_INFO("Exit back ground thread");
}

void AicpuDaemonService::ServiceStop(void *info) const
{
    auto commandToBackGroud = static_cast<CommandToBackGroud *>(info);
    *commandToBackGroud     = CommandToBackGroud::Stop;
    HCCL_INFO("Stop back ground thread");
}

void AicpuDaemonService::Register(DaemonFunc *daemonFunc)
{
    std::unique_lock<std::mutex> lock(mutexForFuncs_);
    daemonFuncs.push_back(daemonFunc);
    HCCL_INFO("Back ground thread register daemonFunc");
}

void AicpuDaemonService::Break()
{
    needBreak = true;
    HCCL_INFO("Back ground thread received break");
}
}