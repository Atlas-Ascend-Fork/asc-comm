/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file hcomm_host_utils.h
 * \brief Hcomm Host utilities
 */

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_UTILS_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_HOST_UTILS_H
#define IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_HOST_UTILS_H

#include <dlfcn.h>
#include <stdint.h>
#include <mutex>

#include "hccl/hccl_res.h"
#include "hcomm/hcomm_res_entity_defs.h"
#include "utils/log/asc_cpu_log.h"

typedef HcclResult (*HcclChannelConfigCreateFunc)(HcclChannelConfig* config);
typedef HcclResult (*HcclChannelConfigDestroyFunc)(HcclChannelConfig config);
typedef HcclResult (*HcclChannelConfigSetIntFunc)(HcclChannelConfig config, HcclChannelConfigType type, uint32_t value);
typedef HcclResult (*HcclChannelConfigSetStrFunc)(
    HcclChannelConfig config, HcclChannelConfigType type, const char* value);
typedef HcclResult (*HcclChannelAcquireWithConfigFunc)(
    HcclComm comm, CommEngine engine, const HcclChannelDesc* channelDescs, uint32_t channelNum,
    HcclChannelConfig config, ChannelHandle* channels);
typedef HcclResult (*HcclEngineCtxCreateFunc)(
    HcclComm comm, const char* ctxTag, CommEngine engine, uint64_t size, void** ctx);
typedef HcclResult (*HcclEngineCtxCopyFunc)(
    HcclComm comm, CommEngine engine, const char* ctxTag, const void* srcCtx, uint64_t size, uint64_t dstCtxOffset);
typedef HcommResult (*HcommChannelConfigCreateFunc)(HcommChannelConfig* config);
typedef HcommResult (*HcommChannelConfigDestroyFunc)(HcommChannelConfig config);
typedef HcommResult (*HcommChannelConfigSetIntFunc)(
    HcommChannelConfig config, HcommChannelConfigType type, uint32_t value);
typedef HcommResult (*HcommChannelCreateWithConfigFunc)(
    EndpointHandle endpointHandle, CommEngine engine, HcommChannelDesc* channelDescs, uint32_t channelNum,
    HcommChannelConfig config, ChannelHandle* channels);
typedef HcommResult (*HcommChannelGetStatusFunc)(
    const ChannelHandle* channelList, uint32_t listNum, int32_t* statusList);
typedef HcommResult (*HcommChannelDestroyFunc)(const ChannelHandle* channels, uint32_t channelNum);

class DlHcommApi {
public:
    static HcclResult LoadHcclApi() { return LoadHcclLibrary(); }

    static HcommResult LoadHcommApi() { return LoadHcommLibrary(); }

    static inline HcclResult HcclChannelConfigCreate(HcclChannelConfig* config)
    {
        return GetHcommApi().HcclChannelConfigCreate(config);
    }

    static inline HcclResult HcclChannelConfigDestroy(HcclChannelConfig config)
    {
        return GetHcommApi().HcclChannelConfigDestroy(config);
    }

    static inline HcclResult HcclChannelConfigSetInt(
        HcclChannelConfig config, HcclChannelConfigType type, uint32_t value)
    {
        return GetHcommApi().HcclChannelConfigSetInt(config, type, value);
    }

    static inline HcclResult HcclChannelConfigSetStr(
        HcclChannelConfig config, HcclChannelConfigType type, const char* value)
    {
        return GetHcommApi().HcclChannelConfigSetStr(config, type, value);
    }

    static inline HcclResult HcclChannelAcquireWithConfig(
        HcclComm comm, CommEngine engine, const HcclChannelDesc* channelDescs, uint32_t channelNum,
        HcclChannelConfig config, ChannelHandle* channels)
    {
        return GetHcommApi().HcclChannelAcquireWithConfig(comm, engine, channelDescs, channelNum, config, channels);
    }

    static inline HcclResult HcclEngineCtxCreate(
        HcclComm comm, const char* ctxTag, CommEngine engine, uint64_t size, void** ctx)
    {
        return GetHcommApi().HcclEngineCtxCreate(comm, ctxTag, engine, size, ctx);
    }

    static inline HcclResult HcclEngineCtxCopy(
        HcclComm comm, CommEngine engine, const char* ctxTag, const void* srcCtx, uint64_t size, uint64_t dstCtxOffset)
    {
        return GetHcommApi().HcclEngineCtxCopy(comm, engine, ctxTag, srcCtx, size, dstCtxOffset);
    }

    static inline HcommResult HcommChannelConfigCreate(HcommChannelConfig* config)
    {
        return GetHcommApi().HcommChannelConfigCreate(config);
    }

    static inline HcommResult HcommChannelConfigDestroy(HcommChannelConfig config)
    {
        return GetHcommApi().HcommChannelConfigDestroy(config);
    }

    static inline HcommResult HcommChannelConfigSetInt(
        HcommChannelConfig config, HcommChannelConfigType type, uint32_t value)
    {
        return GetHcommApi().HcommChannelConfigSetInt(config, type, value);
    }

    static inline HcommResult HcommChannelCreateWithConfig(
        EndpointHandle endpointHandle, CommEngine engine, HcommChannelDesc* channelDescs, uint32_t channelNum,
        HcommChannelConfig config, ChannelHandle* channels)
    {
        return GetHcommApi().HcommChannelCreateWithConfig(
            endpointHandle, engine, channelDescs, channelNum, config, channels);
    }

    static inline HcommResult HcommChannelGetStatus(
        const ChannelHandle* channelList, uint32_t listNum, int32_t* statusList)
    {
        return GetHcommApi().HcommChannelGetStatus(channelList, listNum, statusList);
    }

    static inline HcommResult HcommChannelDestroy(const ChannelHandle* channels, uint32_t channelNum)
    {
        return GetHcommApi().HcommChannelDestroy(channels, channelNum);
    }

private:
    struct HcommApi {
        bool hcclLoaded = false;
        bool hcommLoaded = false;
        HcclChannelConfigCreateFunc HcclChannelConfigCreate = nullptr;
        HcclChannelConfigDestroyFunc HcclChannelConfigDestroy = nullptr;
        HcclChannelConfigSetIntFunc HcclChannelConfigSetInt = nullptr;
        HcclChannelConfigSetStrFunc HcclChannelConfigSetStr = nullptr;
        HcclChannelAcquireWithConfigFunc HcclChannelAcquireWithConfig = nullptr;
        HcclEngineCtxCreateFunc HcclEngineCtxCreate = nullptr;
        HcclEngineCtxCopyFunc HcclEngineCtxCopy = nullptr;
        HcommChannelConfigCreateFunc HcommChannelConfigCreate = nullptr;
        HcommChannelConfigDestroyFunc HcommChannelConfigDestroy = nullptr;
        HcommChannelConfigSetIntFunc HcommChannelConfigSetInt = nullptr;
        HcommChannelCreateWithConfigFunc HcommChannelCreateWithConfig = nullptr;
        HcommChannelGetStatusFunc HcommChannelGetStatus = nullptr;
        HcommChannelDestroyFunc HcommChannelDestroy = nullptr;
    };

    struct HcommLibrary {
        void* handle = nullptr;
        HcommApi hcommApi;
    };

    static HcclResult LoadHcclLibrary()
    {
        std::lock_guard<std::mutex> guard(GetMutex());
        HcommLibrary& library = GetLibrary();
        HcommApi& api = library.hcommApi;
        if (api.hcclLoaded) {
            return HCCL_SUCCESS;
        }
        HcclResult ret = static_cast<HcclResult>(OpenLibrary(library));
        if (ret != HCCL_SUCCESS) {
            return ret;
        }

#define HCOMM_LOAD_SYMBOL(member, symbol)                                                    \
    do {                                                                                     \
        ret = static_cast<HcclResult>(LoadSymbol(library.handle, symbol, loadedApi.member)); \
        if (ret != HCCL_SUCCESS) {                                                           \
            CloseLibraryIfUnused(library);                                                   \
            return ret;                                                                      \
        }                                                                                    \
    } while (0)

        HcommApi loadedApi = api;
        HCOMM_LOAD_SYMBOL(HcclChannelConfigCreate, "HcclChannelConfigCreate");
        HCOMM_LOAD_SYMBOL(HcclChannelConfigDestroy, "HcclChannelConfigDestroy");
        HCOMM_LOAD_SYMBOL(HcclChannelConfigSetInt, "HcclChannelConfigSetInt");
        HCOMM_LOAD_SYMBOL(HcclChannelConfigSetStr, "HcclChannelConfigSetStr");
        HCOMM_LOAD_SYMBOL(HcclChannelAcquireWithConfig, "HcclChannelAcquireWithConfig");
        HCOMM_LOAD_SYMBOL(HcclEngineCtxCreate, "HcclEngineCtxCreate");
        HCOMM_LOAD_SYMBOL(HcclEngineCtxCopy, "HcclEngineCtxCopy");

#undef HCOMM_LOAD_SYMBOL

        loadedApi.hcclLoaded = true;
        api = loadedApi;
        return HCCL_SUCCESS;
    }

    static HcommResult LoadHcommLibrary()
    {
        std::lock_guard<std::mutex> guard(GetMutex());
        HcommLibrary& library = GetLibrary();
        HcommApi& api = library.hcommApi;
        if (api.hcommLoaded) {
            return HCCL_SUCCESS;
        }
        HcommResult ret = static_cast<HcommResult>(OpenLibrary(library));
        if (ret != HCCL_SUCCESS) {
            return ret;
        }

#define HCOMM_LOAD_SYMBOL(member, symbol)                                                     \
    do {                                                                                      \
        ret = static_cast<HcommResult>(LoadSymbol(library.handle, symbol, loadedApi.member)); \
        if (ret != HCCL_SUCCESS) {                                                            \
            CloseLibraryIfUnused(library);                                                    \
            return ret;                                                                       \
        }                                                                                     \
    } while (0)

        HcommApi loadedApi = api;
        HCOMM_LOAD_SYMBOL(HcommChannelConfigCreate, "HcommChannelConfigCreate");
        HCOMM_LOAD_SYMBOL(HcommChannelConfigDestroy, "HcommChannelConfigDestroy");
        HCOMM_LOAD_SYMBOL(HcommChannelConfigSetInt, "HcommChannelConfigSetInt");
        HCOMM_LOAD_SYMBOL(HcommChannelCreateWithConfig, "HcommChannelCreateWithConfig");
        HCOMM_LOAD_SYMBOL(HcommChannelGetStatus, "HcommChannelGetStatus");
        HCOMM_LOAD_SYMBOL(HcommChannelDestroy, "HcommChannelDestroy");

#undef HCOMM_LOAD_SYMBOL

        loadedApi.hcommLoaded = true;
        api = loadedApi;
        return HCCL_SUCCESS;
    }

    static int32_t OpenLibrary(HcommLibrary& library)
    {
        if (library.handle != nullptr) {
            return HCCL_SUCCESS;
        }

        library.handle = dlopen("libhcomm.so", RTLD_NOW | RTLD_LOCAL);
        if (library.handle == nullptr) {
            const char* error = dlerror();
            ASC_CPU_LOG_ERROR(
                "[ERROR] [%s] open libhcomm.so failed: %s.", __func__, error == nullptr ? "unknown error" : error);
            return HCCL_E_NOT_SUPPORT;
        }
        return HCCL_SUCCESS;
    }

    template <typename Func>
    static int32_t LoadSymbol(void* handle, const char* symbol, Func& func)
    {
        (void)dlerror();
        func = reinterpret_cast<Func>(dlsym(handle, symbol));
        const char* error = dlerror();
        if (func == nullptr || error != nullptr) {
            ASC_CPU_LOG_ERROR(
                "[ERROR] [%s] load %s failed: %s.", __func__, symbol, error == nullptr ? "symbol not found" : error);
            return HCCL_E_NOT_SUPPORT;
        }
        return HCCL_SUCCESS;
    }

    static HcommApi& GetHcommApi() { return GetLibrary().hcommApi; }

    static HcommLibrary& GetLibrary()
    {
        static HcommLibrary library;
        return library;
    }

    static std::mutex& GetMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    static void CloseLibraryIfUnused(HcommLibrary& library)
    {
        if (library.hcommApi.hcclLoaded || library.hcommApi.hcommLoaded) {
            return;
        }
        if (library.handle != nullptr) {
            (void)dlclose(library.handle);
        }
        library.handle = nullptr;
    }
};

#endif // IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_HOST_UTILS_H

#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_UTILS_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_UTILS_H
#endif
