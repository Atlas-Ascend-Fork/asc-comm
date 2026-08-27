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
#include <stdio.h>
#include <mutex>

#include "hccl/hccl_res.h"
#include "hcomm/hcomm_res_entity_defs.h"

typedef void* HcommHostChannelConfig;
typedef int32_t HcommHostChannelConfigType;

typedef HcclResult (*HcommHcclChannelConfigCreateFunc)(HcommHostChannelConfig* config);
typedef HcclResult (*HcommHcclChannelConfigDestroyFunc)(HcommHostChannelConfig config);
typedef HcclResult (*HcommHcclChannelConfigSetIntFunc)(
    HcommHostChannelConfig config, HcommHostChannelConfigType type, uint32_t value);
typedef HcclResult (*HcommHcclChannelConfigSetStrFunc)(
    HcommHostChannelConfig config, HcommHostChannelConfigType type, const char* value);
typedef HcclResult (*HcommHcclChannelAcquireWithConfigFunc)(
    HcclComm comm, CommEngine engine, const HcclChannelDesc* channelDescs, uint32_t channelNum,
    HcommHostChannelConfig config, ChannelHandle* channels);
typedef HcclResult (*HcommHcclEngineCtxCreateFunc)(
    HcclComm comm, const char* ctxTag, CommEngine engine, uint64_t size, void** ctx);
typedef HcclResult (*HcommHcclEngineCtxCopyFunc)(
    HcclComm comm, CommEngine engine, const char* ctxTag, const void* srcCtx, uint64_t size, uint64_t dstCtxOffset);
typedef HcclResult (*HcommHcclEngineCtxDestroyFunc)(HcclComm comm, const char* ctxTag, CommEngine engine);
typedef HcommResult (*HcommChannelConfigCreateFunc)(HcommHostChannelConfig* config);
typedef HcommResult (*HcommChannelConfigDestroyFunc)(HcommHostChannelConfig config);
typedef HcommResult (*HcommChannelConfigSetIntFunc)(
    HcommHostChannelConfig config, HcommHostChannelConfigType type, uint32_t value);
typedef HcommResult (*HcommChannelCreateWithConfigFunc)(
    EndpointHandle endpointHandle, CommEngine engine, HcommChannelDesc* channelDescs, uint32_t channelNum,
    HcommHostChannelConfig config, ChannelHandle* channels);
typedef HcommResult (*HcommChannelGetStatusFunc)(
    const ChannelHandle* channelList, uint32_t listNum, int32_t* statusList);
typedef HcommResult (*HcommChannelDestroyFunc)(const ChannelHandle* channels, uint32_t channelNum);

class DlHcommApi {
public:
    static HcclResult LoadHcclApi() { return LoadHcclLibrary(); }

    static HcommResult LoadHcommApi() { return static_cast<HcommResult>(LoadHcommLibrary()); }

    static inline HcclResult HcclChannelConfigCreate(HcommHostChannelConfig* config)
    {
        return GetHcclApi().HcclChannelConfigCreate(config);
    }

    static inline HcclResult HcclChannelConfigDestroy(HcommHostChannelConfig config)
    {
        return GetHcclApi().HcclChannelConfigDestroy(config);
    }

    static inline HcclResult HcclChannelConfigSetInt(
        HcommHostChannelConfig config, HcommHostChannelConfigType type, uint32_t value)
    {
        return GetHcclApi().HcclChannelConfigSetInt(config, type, value);
    }

    static inline HcclResult HcclChannelConfigSetStr(
        HcommHostChannelConfig config, HcommHostChannelConfigType type, const char* value)
    {
        return GetHcclApi().HcclChannelConfigSetStr(config, type, value);
    }

    static inline HcclResult HcclChannelAcquireWithConfig(
        HcclComm comm, CommEngine engine, const HcclChannelDesc* channelDescs, uint32_t channelNum,
        HcommHostChannelConfig config, ChannelHandle* channels)
    {
        return GetHcclApi().HcclChannelAcquireWithConfig(comm, engine, channelDescs, channelNum, config, channels);
    }

    static inline HcclResult HcclEngineCtxCreate(
        HcclComm comm, const char* ctxTag, CommEngine engine, uint64_t size, void** ctx)
    {
        return GetHcclApi().HcclEngineCtxCreate(comm, ctxTag, engine, size, ctx);
    }

    static inline HcclResult HcclEngineCtxCopy(
        HcclComm comm, CommEngine engine, const char* ctxTag, const void* srcCtx, uint64_t size, uint64_t dstCtxOffset)
    {
        return GetHcclApi().HcclEngineCtxCopy(comm, engine, ctxTag, srcCtx, size, dstCtxOffset);
    }

    static inline HcclResult HcclEngineCtxDestroy(HcclComm comm, const char* ctxTag, CommEngine engine)
    {
        return GetHcclApi().HcclEngineCtxDestroy(comm, ctxTag, engine);
    }

    static inline HcommResult HcommChannelConfigCreate(HcommHostChannelConfig* config)
    {
        return GetHcommApi().HcommChannelConfigCreate(config);
    }

    static inline HcommResult HcommChannelConfigDestroy(HcommHostChannelConfig config)
    {
        return GetHcommApi().HcommChannelConfigDestroy(config);
    }

    static inline HcommResult HcommChannelConfigSetInt(
        HcommHostChannelConfig config, HcommHostChannelConfigType type, uint32_t value)
    {
        return GetHcommApi().HcommChannelConfigSetInt(config, type, value);
    }

    static inline HcommResult HcommChannelCreateWithConfig(
        EndpointHandle endpointHandle, CommEngine engine, HcommChannelDesc* channelDescs, uint32_t channelNum,
        HcommHostChannelConfig config, ChannelHandle* channels)
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
    struct HcclApi {
        bool loaded = false;
        HcommHcclChannelConfigCreateFunc HcclChannelConfigCreate = nullptr;
        HcommHcclChannelConfigDestroyFunc HcclChannelConfigDestroy = nullptr;
        HcommHcclChannelConfigSetIntFunc HcclChannelConfigSetInt = nullptr;
        HcommHcclChannelConfigSetStrFunc HcclChannelConfigSetStr = nullptr;
        HcommHcclChannelAcquireWithConfigFunc HcclChannelAcquireWithConfig = nullptr;
        HcommHcclEngineCtxCreateFunc HcclEngineCtxCreate = nullptr;
        HcommHcclEngineCtxCopyFunc HcclEngineCtxCopy = nullptr;
        HcommHcclEngineCtxDestroyFunc HcclEngineCtxDestroy = nullptr;
    };

    struct HcommApi {
        bool loaded = false;
        HcommChannelConfigCreateFunc HcommChannelConfigCreate = nullptr;
        HcommChannelConfigDestroyFunc HcommChannelConfigDestroy = nullptr;
        HcommChannelConfigSetIntFunc HcommChannelConfigSetInt = nullptr;
        HcommChannelCreateWithConfigFunc HcommChannelCreateWithConfig = nullptr;
        HcommChannelGetStatusFunc HcommChannelGetStatus = nullptr;
        HcommChannelDestroyFunc HcommChannelDestroy = nullptr;
    };

    struct HcommLibrary {
        void* handle = nullptr;
        HcclApi hcclApi;
        HcommApi hcommApi;
    };

    static HcclResult LoadHcclLibrary()
    {
        std::lock_guard<std::mutex> guard(GetMutex());
        HcommLibrary& library = GetLibrary();
        HcclApi& api = library.hcclApi;
        if (api.loaded) {
            return HCCL_SUCCESS;
        }
        HcclResult ret = OpenLibrary(library);
        if (ret != HCCL_SUCCESS) {
            return ret;
        }

#define HCOMM_LOAD_SYMBOL(member, symbol)                     \
    do {                                                      \
        ret = LoadSymbol(library.handle, symbol, api.member); \
        if (ret != HCCL_SUCCESS) {                            \
            api = HcclApi{};                                  \
            CloseLibraryIfUnused(library);                    \
            return ret;                                       \
        }                                                     \
    } while (0)

        HCOMM_LOAD_SYMBOL(HcclChannelConfigCreate, "HcclChannelConfigCreate");
        HCOMM_LOAD_SYMBOL(HcclChannelConfigDestroy, "HcclChannelConfigDestroy");
        HCOMM_LOAD_SYMBOL(HcclChannelConfigSetInt, "HcclChannelConfigSetInt");
        HCOMM_LOAD_SYMBOL(HcclChannelConfigSetStr, "HcclChannelConfigSetStr");
        HCOMM_LOAD_SYMBOL(HcclChannelAcquireWithConfig, "HcclChannelAcquireWithConfig");
        HCOMM_LOAD_SYMBOL(HcclEngineCtxCreate, "HcclEngineCtxCreate");
        HCOMM_LOAD_SYMBOL(HcclEngineCtxCopy, "HcclEngineCtxCopy");
        HCOMM_LOAD_SYMBOL(HcclEngineCtxDestroy, "HcclEngineCtxDestroy");

#undef HCOMM_LOAD_SYMBOL

        api.loaded = true;
        return HCCL_SUCCESS;
    }

    static HcclResult LoadHcommLibrary()
    {
        std::lock_guard<std::mutex> guard(GetMutex());
        HcommLibrary& library = GetLibrary();
        HcommApi& api = library.hcommApi;
        if (api.loaded) {
            return HCCL_SUCCESS;
        }
        HcclResult ret = OpenLibrary(library);
        if (ret != HCCL_SUCCESS) {
            return ret;
        }

#define HCOMM_LOAD_SYMBOL(member, symbol)                     \
    do {                                                      \
        ret = LoadSymbol(library.handle, symbol, api.member); \
        if (ret != HCCL_SUCCESS) {                            \
            api = HcommApi{};                                 \
            CloseLibraryIfUnused(library);                    \
            return ret;                                       \
        }                                                     \
    } while (0)

        HCOMM_LOAD_SYMBOL(HcommChannelConfigCreate, "HcommChannelConfigCreate");
        HCOMM_LOAD_SYMBOL(HcommChannelConfigDestroy, "HcommChannelConfigDestroy");
        HCOMM_LOAD_SYMBOL(HcommChannelConfigSetInt, "HcommChannelConfigSetInt");
        HCOMM_LOAD_SYMBOL(HcommChannelCreateWithConfig, "HcommChannelCreateWithConfig");
        HCOMM_LOAD_SYMBOL(HcommChannelGetStatus, "HcommChannelGetStatus");
        HCOMM_LOAD_SYMBOL(HcommChannelDestroy, "HcommChannelDestroy");

#undef HCOMM_LOAD_SYMBOL

        api.loaded = true;
        return HCCL_SUCCESS;
    }

    static HcclResult OpenLibrary(HcommLibrary& library)
    {
        if (library.handle != nullptr) {
            return HCCL_SUCCESS;
        }

        library.handle = dlopen("libhcomm.so", RTLD_NOW | RTLD_LOCAL);
        if (library.handle == nullptr) {
            const char* error = dlerror();
            fprintf(
                stderr, "[ERROR] [%s] open libhcomm.so failed: %s.\n", __func__,
                error == nullptr ? "unknown error" : error);
            return HCCL_E_NOT_SUPPORT;
        }
        return HCCL_SUCCESS;
    }

    template <typename Func>
    static HcclResult LoadSymbol(void* handle, const char* symbol, Func& func)
    {
        (void)dlerror();
        func = reinterpret_cast<Func>(dlsym(handle, symbol));
        const char* error = dlerror();
        if (func == nullptr || error != nullptr) {
            fprintf(
                stderr, "[ERROR] [%s] load %s failed: %s.\n", __func__, symbol,
                error == nullptr ? "symbol not found" : error);
            return HCCL_E_NOT_SUPPORT;
        }
        return HCCL_SUCCESS;
    }

    static HcclApi& GetHcclApi() { return GetLibrary().hcclApi; }

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
        if (library.hcclApi.loaded || library.hcommApi.loaded) {
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
