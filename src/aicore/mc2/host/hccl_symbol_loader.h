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
 * \file hccl_symbol_loader.h
 * \brief
 */

#pragma once
#include <unordered_map>
#include <string>
#include <mutex>
#include <climits>
#include <cstdlib>
#include <dlfcn.h>
#include "../../detail/host_log.h"

namespace HcclApi {
class HcclSymbolLoader {
public:
    static HcclSymbolLoader& GetInstance()
    {
        static HcclSymbolLoader instance;
        return instance;
    }

    template <typename FuncPtr>
    FuncPtr Load(const std::string& soName, const std::string& funcName, const std::string& pathName)
    {
        std::lock_guard<std::mutex> lock(mtx_);

        const std::string funcKey = soName + "::" + funcName;
        const auto it = symbolMap_.find(funcKey);
        if (it != symbolMap_.end()) {
            TILING_LOG_DEBUG("Symbol %s in %s is already loaded.", funcName.c_str(), soName.c_str());
            return reinterpret_cast<FuncPtr>(it->second);
        }

        if (libMap_.find(soName) == libMap_.end()) {
            std::string soPath = pathName + soName;
            char realPath[PATH_MAX] = {0};
            if (realpath(soPath.c_str(), realPath) == nullptr) {
                TILING_LOG_WARNING("Invalid so path %s, it is not a real existing path.", soPath.c_str());
                return nullptr;
            }
            libMap_[soName] = dlopen(realPath, RTLD_LAZY | RTLD_LOCAL);
        }

        if (libMap_[soName] == nullptr) {
            TILING_LOG_WARNING("Failed to open so %s.", soName.c_str());
            return nullptr;
        }

        void* symbol = dlsym(libMap_[soName], funcName.c_str());
        if (symbol == nullptr) {
            TILING_LOG_WARNING("Failed to load symbol %s in so %s.", funcName.c_str(), soName.c_str());
            return nullptr;
        }

        symbolMap_[funcKey] = symbol;
        TILING_LOG_INFO("Symbol %s in %s is successfully loaded.", funcName.c_str(), soName.c_str());
        return reinterpret_cast<FuncPtr>(symbol);
    }

private:
    std::unordered_map<std::string, void*> libMap_{};
    std::unordered_map<std::string, void*> symbolMap_{};
    mutable std::mutex mtx_;
    ~HcclSymbolLoader()
    {
        for (auto& it : libMap_) {
            if (it.second == nullptr) {
                continue;
            }
            (void)dlclose(it.second);
        }
    }
};
} // namespace HcclApi
