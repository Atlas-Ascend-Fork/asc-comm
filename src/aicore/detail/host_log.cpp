/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file host_log.cpp
 * \brief
 */

#include <dlfcn.h>
#include <cstdio>
#include <csignal>
#include "host_log.h"

namespace AscendC {
namespace UnifiedLog {
std::mutex LoggingSingleton::mutex_;
LoggingSingleton* LoggingSingleton::instance_ = nullptr;

LoggingSingleton::LoggingSingleton()
{
    dLogHandle = dlopen("libunified_dlog.so", RTLD_LAZY);
    logHandle = dLogHandle ? dLogHandle : dlopen("libslog.so", RTLD_LAZY);
    CheckLogLevel = reinterpret_cast<int32_t (*)(int32_t, int32_t)>(dlsym(logHandle, "CheckLogLevel"));
    DlogRecord = reinterpret_cast<void (*)(int32_t, int32_t, const char*, ...)>(dlsym(logHandle, "DlogRecord"));
    CheckLogLibFuncApi(CheckLogLevel, DlogRecord);
}

LoggingSingleton::~LoggingSingleton()
{
    if (logHandle != nullptr) {
        dlclose(logHandle);
        logHandle = nullptr;
    }
}

LoggingSingleton* LoggingSingleton::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (instance_ == nullptr) {
        instance_ = new LoggingSingleton();
    }
    return instance_;
}

void LoggingSingleton::CheckLogLibFuncApi(
    int32_t (*checkLogLevel)(int32_t, int32_t), void (*dlogRecord)(int32_t, int32_t, const char*, ...)) const
{
    if (checkLogLevel == nullptr || dlogRecord == nullptr) {
        printf("[ERROR][%s:%d][%s] Can not get function from log library. \n", __FILE__, __LINE__, __FUNCTION__);
        raise(SIGABRT);
    }
}
} // namespace UnifiedLog

UnifiedLog::LoggingSingleton* logInstance = UnifiedLog::LoggingSingleton::getInstance();
} // namespace AscendC