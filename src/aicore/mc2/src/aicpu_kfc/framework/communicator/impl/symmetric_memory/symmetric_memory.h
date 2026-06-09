/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#ifndef SYMMETRIC_MEMORY_H
#define SYMMETRIC_MEMORY_H

#include "log.h"
#include <vector>
#include <unordered_map>
#include <map>
#include <mutex>
#include <functional>
#include <memory>
#include "symmetric_memory_agent.h"
#include "hccl_mem_alloc.h"

// HCCL
#include "hccl/base.h"
#include "hccl_comm.h"
#include "adapter_rts_common.h"
#include "hccl_inner.h"

// NPU VMM API
#include "acl/acl_rt.h"

namespace hccl {

struct FreeBlock {
    size_t offset;
    size_t size;
};

struct ShareableInfo {
    size_t offset;
    size_t size;
    aclrtMemFabricHandle handle;
};

struct SymmetricWindow {
    void* userVa;
    size_t userSize;

    void* baseVa;               // 对应userVa在对称堆上的地址
    size_t alignedHeapOffset;
    size_t alignedSize;
    u32 localRank;
    u32 rankSize;
    size_t stride;
    aclrtDrvMemHandle paHandle;

    void* devWin; // device端结构体
};

struct PaMappingInfo {
    aclrtDrvMemHandle paHandle;             // 唯一标识：PA 句柄
    aclrtMemFabricHandle shareableHandle;    //  对应的共享句柄

    // 这里需要记录原始 allocation 的起始 VA (例如 0x1000) 和总大小 (100MB)
    void* origAllocBaseVa;
    size_t origAllocSize;

    // 对称堆上的映射信息
    // 这块物理内存在对称堆上的起始 offset (例如在 heapBase + 0x5000)
    size_t heapBaseOffset;

    // 引用计数：有多少个 Window 正在复用这块物理内存
    // 当 refCount 降为 0 时，才执行 Unmap 和 Release VA
    u32 refCount;
};

/**
 * @brief 对称内存管理器
 * 负责对称VA空间的预留、注册、映射和查找。
 */
class SymmetricMemory {
public:
    SymmetricMemory(u32 rank, u32 rankSize, size_t stride, std::shared_ptr<SymmetricMemoryAgent> symmetricMemoryAgent);
    ~SymmetricMemory();

    // 禁止拷贝和赋值
    SymmetricMemory(const SymmetricMemory&) = delete;
    SymmetricMemory& operator=(const SymmetricMemory&) = delete;
    HcclResult EnsureInit();
    void* AllocSymmetricMem(size_t size);
    HcclResult FreeSymmetricMem(void* devWin);
    HcclResult GetMemoryInfo(void* ptr, size_t size, void** baseUserVa, size_t* baseVaSize, aclrtDrvMemHandle* paHandle);
    HcclResult RegisterSymmetricMem(void* ptr, size_t size, void** devWin);
    HcclResult DeregisterSymmetricMem(void* devWin);
    HcclResult FindSymmetricWindow(void* ptr, size_t size, void** win, u64 *offset);

private:
    HcclResult Init();
    HcclResult GetAllRankPid();
    HcclResult RegisterInternal(aclrtDrvMemHandle &paHandle, size_t offset, size_t mapSize);
    HcclResult AddSymmetricWindow(std::shared_ptr<SymmetricWindow> &win);
    HcclResult DeleteSymmetricWindow(std::shared_ptr<SymmetricWindow> &win);
    HcclResult DeleteSymmetricWindow(void* devWin);

private:
    // VA空间分配器 (Pimpl)
    std::once_flag init_flag_;
    u32 rank_{0};
    u32 rankSize_{0};
    size_t stride_{0};      // 每个Rank的VA空间大小
    void* heapBase_{nullptr};  // 对称VA空间的总基地址 (所有rank相同)
    size_t granularity_{0};
    class SimpleVaAllocator;
    std::unique_ptr<SimpleVaAllocator> vaAllocator_;
    HcclResult initResult_{HCCL_E_INTERNAL}; // 存储Init()的结果
    std::vector<std::shared_ptr<SymmetricWindow>> sortedWindows_;
    std::map<void*, std::shared_ptr<SymmetricWindow>> windowMap_; // device指针到host SymmetricWindow 的映射
    std::unordered_map<aclrtDrvMemHandle, std::shared_ptr<PaMappingInfo>> paMappingMap_;
    std::shared_ptr<SymmetricMemoryAgent> symmetricMemoryAgent_;
    std::vector<int32_t> remoteShareablePids;   // 所有rank进程号
    aclrtPhysicalMemProp prop = {              // 内存信息，用来获取内存映射的粒度
        ACL_MEM_HANDLE_TYPE_NONE,
        ACL_MEM_ALLOCATION_TYPE_PINNED,
        ACL_HBM_MEM_HUGE,
        {0, ACL_MEM_LOCATION_TYPE_DEVICE},
        0
    };
    size_t targetStartTB = 40ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;   //  从40TB处预留虚拟内存
    std::unordered_map<void*, aclrtDrvMemHandle> importAddrs_{};    // 记录虚拟内存映射的物理内存，用于资源释放。
    bool isSingleRank_{false};
};

} // namespace hccl

#endif // SYMMETRIC_MEMORY_H