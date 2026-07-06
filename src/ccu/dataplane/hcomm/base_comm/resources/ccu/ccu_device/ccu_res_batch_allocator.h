/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_RES_BATCH_ALLOCATOR_H
#define CCU_RES_BATCH_ALLOCATOR_H

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "ccu_res_specs.h"
#include "ccu_dev_mgr_imp.h"

namespace hcomm {

struct BlockInfo {
    uint32_t id{0};
    uint32_t startId{0};
    uint32_t num{0};
    uintptr_t handle{0};
    bool allocated{false};
};

class CcuResBatchAllocator {
public:
    static CcuResBatchAllocator& GetInstance(const int32_t deviceLogicId);
    HcclResult Init();
    HcclResult Deinit();

    HcclResult AllocResHandle(const CcuResReq& resReq, CcuResHandle &resHandle);
    HcclResult ReleaseResHandle(const CcuResHandle& handle);
    HcclResult GetResource(const CcuResHandle& handle, CcuResRepository &ccuResRepo);

private:
    class CcuMissionMgr {
    public:
        CcuMissionMgr() = default;
        ~CcuMissionMgr() = default;

        void Reset();
        HcclResult PreAlloc(const int32_t devLogicId, const uint32_t blockSize,
            const std::array<bool, CCU_MAX_IODIE_NUM> &dieFlags);
        HcclResult Alloc(const uintptr_t handleKey, const MissionReq &missionReq,
            MissionResInfo &missionInfos);
        void Release(MissionResInfo &missionInfos);
    
    private:
        uint32_t stragtegy_{0};
        std::array<bool, CCU_MAX_IODIE_NUM> dieEnableFlags_;
        std::vector<BlockInfo> blocks_;
    };

private:
    explicit CcuResBatchAllocator() = default;
    CcuResBatchAllocator(const CcuResBatchAllocator &that) = delete;
    CcuResBatchAllocator& operator=(const CcuResBatchAllocator &that) = delete;
    ~CcuResBatchAllocator() = default; // 不允许在析构中调用CcuComponent，会引起未定义行为

    HcclResult PreAllocBlockRes();
    HcclResult TryAllocResHandle(const uintptr_t handleKey, const CcuResReq& resReq,
        std::unique_ptr<CcuResRepository>& resRepoPtr);
    HcclResult AllocBlockRes(const uintptr_t handleKey, const CcuResReq& resReq,
        std::unique_ptr<CcuResRepository> &resRepoPtr);
    HcclResult AllocConsecutiveRes(const CcuResReq& resReq,
        std::unique_ptr<CcuResRepository>& resRepoPtr) const;
    HcclResult AllocDiscreteRes(const CcuResReq& resReq,
        std::unique_ptr<CcuResRepository>& resRepoPtr) const;
    HcclResult ReleaseResource(std::unique_ptr<CcuResRepository>& resRepoPtr);
    void ReleaseBlockResource(std::unique_ptr<CcuResRepository> &resRepoPtr);
    HcclResult ReleaseNonBlockTypeRes(std::unique_ptr<CcuResRepository>& resRepoPtr) const;

private:
    std::mutex innerMutex_;
    int32_t devLogicId_{0};
    bool initFlag_{false};
    std::array<bool, CCU_MAX_IODIE_NUM> dieEnableFlags_{};
    std::array<CcuBlockResStrategy, CCU_MAX_IODIE_NUM> resStrategys_{};
    std::array<std::vector<std::vector<BlockInfo>>, CCU_MAX_IODIE_NUM> resBlocks_{};
    // 键值为CcuResRepository裸指针转换的uintptr_t
    std::unordered_map<uintptr_t, std::unique_ptr<CcuResRepository>> handleMap_{};
    CcuMissionMgr missionMgr_{};
};

}; // namespace hcomm

#endif // CCU_RES_BATCH_ALLOCATOR_H