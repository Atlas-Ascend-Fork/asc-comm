/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef THREAD_H
#define THREAD_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "hccl_types.h"
#include "hccl_common.h"
#include "hcomm_primitives.h"
#include "local_notify.h"
#include "stream_pub.h"
#include "acl/acl_rt.h"
#include "adapter_hal_pub.h"
#include "device_capacity.h"
#include "task_param.h"
#include "sal_pub.h"
#include "stream_lite.h"
#ifndef CCL_KERNEL_AICPU
#include "adapter_prof.h"
#endif

namespace hccl {

constexpr u32 HCCL_THREAD_NOTIFY_MAX_NUM = 65536;
/**
 * @note 职责：通信引擎的Thread的C++抽象接口类，表达并行资源，内部包含thread间的同步Notify。
 */
class Thread {
public:
    virtual ~Thread() = default;
    virtual HcclResult Init() = 0;
    virtual HcclResult DeInit() = 0;
    virtual std::string &GetUniqueId() = 0;
    virtual uint32_t GetNotifyNum() const = 0;
    virtual LocalNotify *GetNotify(uint32_t index) const = 0;
    virtual HcclResult SupplementNotify(uint32_t notifyNum) = 0;

    virtual bool IsDeviceA5() const = 0;
    virtual Stream *GetStream() const = 0;
    virtual void *GetStreamLitePtr() const = 0;
    virtual void LaunchTask() const = 0;
    virtual void TryLaunchTask() const = 0;

    // Local Data Plane Functions
    virtual HcclResult LocalNotifyRecord(uint32_t notifyId) const = 0;
    virtual HcclResult LocalNotifyWait(uint32_t notifyId) const = 0;

    virtual HcclResult LocalNotifyRecord(ThreadHandle dstThread, uint32_t dstNotifyIdx) const = 0;
    virtual HcclResult LocalNotifyWait(uint32_t notifyIdx, uint32_t timeOut) const = 0;

    virtual HcclResult LocalCopy(void *dst, const void *src, uint64_t sizeByte) const = 0;
    virtual HcclResult LocalReduce(
        void *dst, const void *src, uint64_t sizeByte, HcommDataType dataType, HcommReduceOp reduceOp) const = 0;
    virtual bool GetMaster() const = 0;
    virtual void SetIsMaster(bool isMaster) = 0;

    HcclResult AddThreadHandleToMap(CommEngine commEngine, ThreadHandle threadHandle);
    Thread *FindThreadByCommEngine(CommEngine commEngine);
    HcclResult SetAddTaskInfoCallback(std::function<HcclResult(u32, u32, const Hccl::TaskParam&, u64)> callback)
    {
        CHK_PTR_NULL(callback);
        callback_ = callback;
        return HCCL_SUCCESS;
    }
    std::function<HcclResult(u32, u32, const Hccl::TaskParam&, u64)> GetCallback()
    {
        return callback_;
    }
    virtual HcclResult SetCheckExecStatusCallback(std::function<HcclResult(bool)> callback)
    {
        return HCCL_SUCCESS;
    }
protected:
#ifndef CCL_KERNEL_AICPU
    HcclResult ReportAicpuNotifyWaitTask(u64 notifyId, u64 beginTime, u32 taskId, u32 sqId) const;
    HcclResult ReportHostNotifyWaitTask(u64 notifyId, u64 beginTime, bool isMaster) const;
    HcclResult ReportAicpuNotifyRecordTask(u64 notifyId, u64 beginTime, u32 taskId, u32 sqId) const;
    HcclResult ReportHostNotifyRecordTask(u64 notifyId, u64 beginTime, bool isMaster) const;
    HcclResult ReportAicpuLocalCopyTask(void *dst, const void *src, uint64_t sizeByte, u64 beginTime, u32 taskId,u32 sqId) const;
    HcclResult ReportHostLocalCopyTask(void *dst, const void *src, uint64_t sizeByte, u64 beginTime, bool isMaster) const;
    HcclResult ReportAicpuLocalReduceTask(void *dst, const void *src, uint64_t sizeByte, HcommDataType dataType,
        HcommReduceOp reduceOp, u64 beginTime, u32 taskId, u32 sqId) const;
    HcclResult ReportHostLocalReduceTask(void *dst, const void *src, uint64_t sizeByte, HcommDataType dataType,
        HcommReduceOp reduceOp, u64 beginTime, bool isMaster) const;
    bool IsReportTask() const;
#endif
private:
    std::unordered_map<CommEngine, ThreadHandle> threadHandleMap_; // CPU_TS上的ThreadHandle与其他引擎上的ThreadHandle的映射
    std::function<HcclResult(u32, u32, const Hccl::TaskParam&, u64)> callback_; // 上报task信息的回调函数
};

}  // namespace hccl
#endif  // THREAD_H
