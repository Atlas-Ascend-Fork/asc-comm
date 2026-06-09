/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#ifndef MIRROR_TASK_MANAGER_H
#define MIRROR_TASK_MANAGER_H

#include <map>
#include <unordered_map>
#include <memory>
#include <functional>
#include "circular_queue.h"
#include "task_info.h"
#include "global_mirror_tasks.h"

namespace Hccl {

using TaskInfoQueue = Queue<std::shared_ptr<TaskInfo>>;

class MirrorTaskManager {
public:
    MirrorTaskManager(u32 devId, GlobalMirrorTasks *globalMirrorTasks, bool devUsed);

    void RegFullyCallBack(std::function<void()> callBack);
    void RegFullyCallBack(std::function<void(const std::string&, u32)> callBack);
    void AddTaskInfo(std::shared_ptr<TaskInfo> taskInfo);
    void SetCurrDfxOpInfo(std::shared_ptr<DfxOpInfo> dfxOpInfo);

    std::shared_ptr<DfxOpInfo> GetCurrDfxOpInfo() const;
    TaskInfoQueue             *GetQueue(u32 streamId) const;

public:
    std::unordered_map<u32, TaskInfoQueue *>::iterator Begin();
    std::unordered_map<u32, TaskInfoQueue *>::iterator End();

    ~MirrorTaskManager();

private:
    u32                            devId_;
    GlobalMirrorTasks             *globalMirrorTasks_{nullptr};
    bool                           devUsed_{false};
    bool                           isStaticGraphMode_{false};
    OpMode                         opMode_;
    std::unordered_map<u32, TaskInfoQueue *> queueMap_;
    std::unordered_map<u32, u32> queueTaskNum;
    std::shared_ptr<DfxOpInfo>     currDfxOpInfo_;
    std::function<void()>          fullyCallBack_;
    std::function<void(const std::string&, u32)>          fullyNewCallBack_;

private:
    bool      IsStaticGraphMode(const CollOperator &collOperator) const;
    QueueType GetQueueType() const;
};

} // namespace Hccl

#endif // MIRROR_TASK_MANAGER_H