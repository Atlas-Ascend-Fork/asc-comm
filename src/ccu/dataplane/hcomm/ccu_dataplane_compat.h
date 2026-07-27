/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*
 * Compatibility glue for building migrated hcomm CCU data-plane sources
 * outside the original hcomm target.
 */
#ifndef ASCCOMM_CCU_DATAPLANE_COMPAT_H
#define ASCCOMM_CCU_DATAPLANE_COMPAT_H

#include "log.h"
#include "hccl_exception.h"

using Hccl::CallDlog;
using Hccl::CallDlogInvalidType;
using Hccl::CallDlogMemError;
using Hccl::CallDlogNoSzFormat;
using Hccl::CallDlogPrintError;
using Hccl::HCCL_LOG_DEBUG;
using Hccl::HCCL_LOG_ERROR;
using Hccl::HCCL_LOG_INFO;
using Hccl::HCCL_LOG_OPLOG;
using Hccl::HCCL_LOG_RUN_INFO;
using Hccl::HCCL_LOG_WARN;
using Hccl::HCCL_MODULE_ID;
using Hccl::HcclSubModuleID;
using Hccl::HcclCheckLogLevel;
using Hccl::LOG_TMPBUF_SIZE;
using Hccl::SYSTEM_RESERVE_ERROR;
using Hccl::HcclException;
using std::string;

extern s32 HcclGetThreadDeviceId();

#ifndef HCCL_RUN_WARNING
#define HCCL_RUN_WARNING(...) HCCL_WARNING(__VA_ARGS__)
#endif

#ifndef HCCL_CONFIG_DEBUG
#define HCCL_CONFIG_DEBUG(config, ...) HCCL_DEBUG(__VA_ARGS__)
#endif

#ifndef HCCL_CONFIG_INFO
#define HCCL_CONFIG_INFO(config, ...) HCCL_INFO(__VA_ARGS__)
#endif

#ifndef HCCL_ENTRY_INFO
#define HCCL_ENTRY_INFO(opEntry, ...) HCCL_INFO(__VA_ARGS__)
#endif

#endif // ASCCOMM_CCU_DATAPLANE_COMPAT_H
