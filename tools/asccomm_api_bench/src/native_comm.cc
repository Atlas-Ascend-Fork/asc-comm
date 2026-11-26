/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "native_comm.h"

#include <cstring>
#include <hccl/hccl.h>

namespace asccomm_api_bench {

static_assert(HCCL_SUCCESS == 0, "communication bootstrap requires HCCL_SUCCESS == 0");

CommBootstrapApi MakeNativeCommApi()
{
    CommBootstrapApi api;
    api.root_info_bytes = sizeof(HcclRootInfo);
    api.get_root_info = [](std::string& bytes) -> int64_t {
        HcclRootInfo root = {};
        const HcclResult ret = HcclGetRootInfo(&root);
        if (ret == HCCL_SUCCESS)
            bytes.assign(reinterpret_cast<const char*>(&root), sizeof(root));
        return static_cast<int64_t>(ret);
    };
    api.create = [](uint32_t world, const std::string& bytes, uint32_t rank, uint8_t expansion_mode,
                    void** output) -> int64_t {
        if (bytes.size() != sizeof(HcclRootInfo) || output == nullptr)
            return -1;
        HcclRootInfo root = {};
        std::memcpy(&root, bytes.data(), sizeof(root));
        HcclCommConfig config = {};
        HcclCommConfigInit(&config);
        config.hcclWorldRankID = rank;
        config.hcclOpExpansionMode = expansion_mode;
        HcclComm comm = nullptr;
        const HcclResult ret = HcclCommInitRootInfoConfig(world, &root, rank, &config, &comm);
        *output = comm;
        return static_cast<int64_t>(ret);
    };
    api.get_name = [](void* handle, std::string& output) -> int64_t {
        char name[COMM_NAME_MAX_LENGTH] = {};
        const HcclResult ret = HcclGetCommName(static_cast<HcclComm>(handle), name);
        if (ret != HCCL_SUCCESS)
            return static_cast<int64_t>(ret);
        const char* end = static_cast<const char*>(std::memchr(name, '\0', sizeof(name)));
        if (end == nullptr)
            return -1;
        output.assign(name, static_cast<size_t>(end - name));
        return 0;
    };
    api.destroy = [](void* handle) -> int64_t {
        return static_cast<int64_t>(HcclCommDestroy(static_cast<HcclComm>(handle)));
    };
    return api;
}

} // namespace asccomm_api_bench
