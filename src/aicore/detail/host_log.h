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
 * \file host_log.h
 * \brief
 */
#ifndef IMPL_HOST_LOG_H
#define IMPL_HOST_LOG_H
#include <mutex>
#include "dlog_pub.h"

namespace AscendC {
namespace UnifiedLog {
class LoggingSingleton {
private:
  static LoggingSingleton* instance_;
  static std::mutex mutex_;
  void* dLogHandle;
  void* logHandle;

  LoggingSingleton();
  ~LoggingSingleton();

  void CheckLogLibFuncApi(
    int32_t(*checkLogLevel)(int32_t, int32_t), void(*dlogRecord)(int32_t, int32_t, const char*, ...)) const;

public:
  static LoggingSingleton* getInstance();
  int32_t (*CheckLogLevel)(int32_t, int32_t);
  void (*DlogRecord)(int32_t, int32_t, const char*, ...);
}; // class LoggingSingleton

} // namespace UnifiedLog

extern UnifiedLog::LoggingSingleton* logInstance;
} // namespace AscendC

#define ASCENDC_MODULE_NAME static_cast<int32_t>(ASCENDCKERNEL)

#define ASCENDC_HOST_ASSERT(cond, ret, format, ...)                                         \
  do {                                                                                      \
    if (!(cond)) {                                                                          \
      if (AscendC::logInstance->CheckLogLevel(ASCENDC_MODULE_NAME, DLOG_ERROR) == 1) {               \
        AscendC::logInstance->DlogRecord(ASCENDC_MODULE_NAME | DEBUG_LOG_MASK, DLOG_ERROR,           \
                                "[%s:%d][%s] " format "\n", __FILE__, __LINE__,             \
                                __FUNCTION__, ##__VA_ARGS__);                               \
      }                                                                                     \
      ret;                                                                                  \
    }                                                                                       \
  } while (0)

// 0 debug, 1 info, 2 warning, 3 error
#define TILING_LOG_ERROR(format, ...)                                                       \
  do {                                                                                      \
    if (AscendC::logInstance->CheckLogLevel(ASCENDC_MODULE_NAME, DLOG_ERROR) == 1) {                 \
      AscendC::logInstance->DlogRecord(ASCENDC_MODULE_NAME | DEBUG_LOG_MASK, DLOG_ERROR,             \
                              "[%s:%d][%s] " format "\n", __FILE__, __LINE__,               \
                              __FUNCTION__, ##__VA_ARGS__);                                 \
    }                                                                                       \
  } while (0)
#define TILING_LOG_INFO(format, ...)                                                        \
  do {                                                                                      \
    if (AscendC::logInstance->CheckLogLevel(ASCENDC_MODULE_NAME, DLOG_INFO) == 1) {                  \
      AscendC::logInstance->DlogRecord(ASCENDC_MODULE_NAME | DEBUG_LOG_MASK, DLOG_INFO,              \
                              "[%s:%d][%s] " format "\n", __FILE__, __LINE__,               \
                              __FUNCTION__, ##__VA_ARGS__);                                 \
    }                                                                                       \
  } while (0)
#define TILING_LOG_WARNING(format, ...)                                                     \
  do {                                                                                      \
    if (AscendC::logInstance->CheckLogLevel(ASCENDC_MODULE_NAME, DLOG_WARN) == 1) {                  \
      AscendC::logInstance->DlogRecord(ASCENDC_MODULE_NAME | DEBUG_LOG_MASK, DLOG_WARN,              \
                              "[%s:%d][%s] " format "\n", __FILE__, __LINE__,               \
                              __FUNCTION__, ##__VA_ARGS__);                                 \
    }                                                                                       \
  } while (0)
#define TILING_LOG_DEBUG(format, ...)                                                       \
  do {                                                                                      \
    if (AscendC::logInstance->CheckLogLevel(ASCENDC_MODULE_NAME, DLOG_DEBUG) == 1) {                 \
      AscendC::logInstance->DlogRecord(ASCENDC_MODULE_NAME | DEBUG_LOG_MASK, DLOG_DEBUG,             \
                              "[%s:%d][%s] " format "\n", __FILE__, __LINE__,               \
                              __FUNCTION__, ##__VA_ARGS__);                                 \
    }                                                                                       \
  } while (0)
#endif // IMPL_HOST_LOG_H
