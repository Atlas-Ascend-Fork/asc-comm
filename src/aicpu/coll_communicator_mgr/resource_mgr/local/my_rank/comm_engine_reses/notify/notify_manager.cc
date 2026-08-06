/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "notify_manager.h"
#include "adapter_hal_pub.h"

namespace hccl {
HcclResult NotifyManager::InitNotifys(std::istringstream &iss, size_t notifyNum,
    std::vector<std::unique_ptr<LocalNotify>> &newNotifys)
{
    newNotifys.reserve(newNotifys.size() + notifyNum);
    for (u32 idx = 0; idx < notifyNum; idx++) {
        std::unique_ptr<LocalNotify> notify;
        EXCEPTION_CATCH(notify = std::make_unique<LocalNotify>(), return HCCL_E_PTR);
        HcclSignalInfo notifyInfo;
        iss.read(reinterpret_cast<char_t *>(&notifyInfo), sizeof(notifyInfo));
        CHK_RET(notify->Init(notifyInfo, NotifyLoadType::DEVICE_NOTIFY));
        newNotifys.emplace_back(std::move(notify));
        HCCL_INFO("[NotifyManager][Init]local notify init success, resId[%u], tsId:%d, devId[%u]",
            notifyInfo.resId, notifyInfo.tsId, notifyInfo.devId);
    }
    return HCCL_SUCCESS;
}

HcclResult NotifyManager::ParseBinNotifys(const std::string& uniqueIdStr,
    std::vector<std::unique_ptr<LocalNotify>> &newNotifys)
{
    bool isDeviceSid = false;
    CHK_RET(GetRunSideIsDevice(isDeviceSid));
    NotifyLoadType loadType;
    size_t notifyNum = 0;
    if (!isDeviceSid) {
        HCCL_ERROR("[NotifyManager][%s] not in deviceSide", __func__);
        return HCCL_E_NOT_SUPPORT;
    } else {
        CHK_PRT_RET(uniqueIdStr.empty(), HCCL_ERROR("[HcclThread][%s] uniqueIdStr is empty"), HCCL_E_INTERNAL);
        std::istringstream iss(uniqueIdStr);
        iss.read(reinterpret_cast<char_t *>(&loadType), sizeof(loadType));
        iss.read(reinterpret_cast<char_t *>(&notifyNum), sizeof(notifyNum));
        CHK_RET(InitNotifys(iss, notifyNum, newNotifys));
    }
    HCCL_RUN_INFO("[NotifyManager][%s] recover success, notifyNum[%zu], notifyType[%d], uniqueIdSize[%zu]",
        __func__, notifyNum, loadType, uniqueIdStr.size());
    return HCCL_SUCCESS;
}
}
