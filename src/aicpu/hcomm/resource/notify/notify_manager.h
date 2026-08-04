/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef NOTIFY_MANAGER_H
#define NOTIFY_MANAGER_H

#include "local_notify.h"

#include <sstream>

namespace hccl {

// NOTE: NotifyManager 被 coll_comm_aicpu 依赖。
class NotifyManager {
public:
    static HcclResult ParseBinNotifys(const std::string& uniqueIdStr,
        std::vector<std::unique_ptr<LocalNotify>> &newNotifys);
private:
    static HcclResult InitNotifys(std::istringstream &iss, size_t notifyNum,
        std::vector<std::unique_ptr<LocalNotify>> &newNotifys);
};
}

#endif
