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
typedef HcommResult (*HcommEngineCtxCreateFunc)(CommEngine engine, uint64_t size, void** ctx);
typedef HcommResult (*HcommEngineCtxCopyFunc)(CommEngine engine, void* dstCtx, const void* srcCtx, uint64_t size);
typedef HcommResult (*HcommEngineCtxDestroyFunc)(CommEngine engine, void* ctx);

class DlHcommApi {
public:
    static HcclResult LoadHcclApi() { return LoadLibrary(ApiGroup::HCCL); }

    static HcommResult LoadHcommApi() { return static_cast<HcommResult>(LoadLibrary(ApiGroup::HCOMM)); }

    static void CleanupLibrary()
    {
        Api& api = GetApi();
        if (api.referenceCount == 0U) {
            return;
        }
        --api.referenceCount;
        if (api.referenceCount == 0U) {
            CloseLibrary(api);
        }
    }

    static inline HcclResult HcclChannelConfigCreate(HcommHostChannelConfig* config)
    {
        return GetApi().HcclChannelConfigCreate(config);
    }

    static inline HcclResult HcclChannelConfigDestroy(HcommHostChannelConfig config)
    {
        return GetApi().HcclChannelConfigDestroy(config);
    }

    static inline HcclResult HcclChannelConfigSetInt(
        HcommHostChannelConfig config, HcommHostChannelConfigType type, uint32_t value)
    {
        return GetApi().HcclChannelConfigSetInt(config, type, value);
    }

    static inline HcclResult HcclChannelConfigSetStr(
        HcommHostChannelConfig config, HcommHostChannelConfigType type, const char* value)
    {
        return GetApi().HcclChannelConfigSetStr(config, type, value);
    }

    static inline HcclResult HcclChannelAcquireWithConfig(
        HcclComm comm, CommEngine engine, const HcclChannelDesc* channelDescs, uint32_t channelNum,
        HcommHostChannelConfig config, ChannelHandle* channels)
    {
        return GetApi().HcclChannelAcquireWithConfig(comm, engine, channelDescs, channelNum, config, channels);
    }

    static inline HcclResult HcclEngineCtxCreate(
        HcclComm comm, const char* ctxTag, CommEngine engine, uint64_t size, void** ctx)
    {
        return GetApi().HcclEngineCtxCreate(comm, ctxTag, engine, size, ctx);
    }

    static inline HcclResult HcclEngineCtxCopy(
        HcclComm comm, CommEngine engine, const char* ctxTag, const void* srcCtx, uint64_t size, uint64_t dstCtxOffset)
    {
        return GetApi().HcclEngineCtxCopy(comm, engine, ctxTag, srcCtx, size, dstCtxOffset);
    }

    static inline HcclResult HcclEngineCtxDestroy(HcclComm comm, const char* ctxTag, CommEngine engine)
    {
        return GetApi().HcclEngineCtxDestroy(comm, ctxTag, engine);
    }

    static inline HcommResult HcommChannelConfigCreate(HcommHostChannelConfig* config)
    {
        return GetApi().HcommChannelConfigCreate(config);
    }

    static inline HcommResult HcommChannelConfigDestroy(HcommHostChannelConfig config)
    {
        return GetApi().HcommChannelConfigDestroy(config);
    }

    static inline HcommResult HcommChannelConfigSetInt(
        HcommHostChannelConfig config, HcommHostChannelConfigType type, uint32_t value)
    {
        return GetApi().HcommChannelConfigSetInt(config, type, value);
    }

    static inline HcommResult HcommChannelCreateWithConfig(
        EndpointHandle endpointHandle, CommEngine engine, HcommChannelDesc* channelDescs, uint32_t channelNum,
        HcommHostChannelConfig config, ChannelHandle* channels)
    {
        return GetApi().HcommChannelCreateWithConfig(
            endpointHandle, engine, channelDescs, channelNum, config, channels);
    }

    static inline HcommResult HcommChannelGetStatus(
        const ChannelHandle* channelList, uint32_t listNum, int32_t* statusList)
    {
        return GetApi().HcommChannelGetStatus(channelList, listNum, statusList);
    }

    static inline HcommResult HcommChannelDestroy(const ChannelHandle* channels, uint32_t channelNum)
    {
        return GetApi().HcommChannelDestroy(channels, channelNum);
    }

    static inline HcommResult HcommEngineCtxCreate(CommEngine engine, uint64_t size, void** ctx)
    {
        return GetApi().HcommEngineCtxCreate(engine, size, ctx);
    }

    static inline HcommResult HcommEngineCtxCopy(CommEngine engine, void* dstCtx, const void* srcCtx, uint64_t size)
    {
        return GetApi().HcommEngineCtxCopy(engine, dstCtx, srcCtx, size);
    }

    static inline HcommResult HcommEngineCtxDestroy(CommEngine engine, void* ctx)
    {
        return GetApi().HcommEngineCtxDestroy(engine, ctx);
    }

private:
    enum class ApiGroup : uint32_t {
        NONE = 0U,
        HCCL,
        HCOMM,
    };

    struct Api {
        void* handle = nullptr;
        uint32_t referenceCount = 0U;
        ApiGroup group = ApiGroup::NONE;
        HcommHcclChannelConfigCreateFunc HcclChannelConfigCreate = nullptr;
        HcommHcclChannelConfigDestroyFunc HcclChannelConfigDestroy = nullptr;
        HcommHcclChannelConfigSetIntFunc HcclChannelConfigSetInt = nullptr;
        HcommHcclChannelConfigSetStrFunc HcclChannelConfigSetStr = nullptr;
        HcommHcclChannelAcquireWithConfigFunc HcclChannelAcquireWithConfig = nullptr;
        HcommHcclEngineCtxCreateFunc HcclEngineCtxCreate = nullptr;
        HcommHcclEngineCtxCopyFunc HcclEngineCtxCopy = nullptr;
        HcommHcclEngineCtxDestroyFunc HcclEngineCtxDestroy = nullptr;
        HcommChannelConfigCreateFunc HcommChannelConfigCreate = nullptr;
        HcommChannelConfigDestroyFunc HcommChannelConfigDestroy = nullptr;
        HcommChannelConfigSetIntFunc HcommChannelConfigSetInt = nullptr;
        HcommChannelCreateWithConfigFunc HcommChannelCreateWithConfig = nullptr;
        HcommChannelGetStatusFunc HcommChannelGetStatus = nullptr;
        HcommChannelDestroyFunc HcommChannelDestroy = nullptr;
        HcommEngineCtxCreateFunc HcommEngineCtxCreate = nullptr;
        HcommEngineCtxCopyFunc HcommEngineCtxCopy = nullptr;
        HcommEngineCtxDestroyFunc HcommEngineCtxDestroy = nullptr;
    };

    static HcclResult LoadLibrary(ApiGroup group)
    {
        Api& api = GetApi();
        if (api.referenceCount != 0U) {
            if (api.group != group) {
                fprintf(stderr, "[ERROR] [%s] cannot load HCCL and Hcomm Host APIs in one scope.\n", __func__);
                return HCCL_E_INTERNAL;
            }
            ++api.referenceCount;
            return HCCL_SUCCESS;
        }

        api.handle = dlopen("libhcomm.so", RTLD_NOW | RTLD_LOCAL);
        if (api.handle == nullptr) {
            const char* error = dlerror();
            fprintf(
                stderr, "[ERROR] [%s] open libhcomm.so failed: %s.\n", __func__,
                error == nullptr ? "unknown error" : error);
            return HCCL_E_NOT_SUPPORT;
        }

#define HCOMM_LOAD_SYMBOL(member, type, symbol)                                 \
    do {                                                                        \
        (void)dlerror();                                                        \
        api.member = reinterpret_cast<type>(dlsym(api.handle, symbol));         \
        const char* hcommDlError = dlerror();                                   \
        if (api.member == nullptr || hcommDlError != nullptr) {                 \
            fprintf(                                                            \
                stderr, "[ERROR] [%s] load %s failed: %s.\n", __func__, symbol, \
                hcommDlError == nullptr ? "symbol not found" : hcommDlError);   \
            CloseLibrary(api);                                                  \
            return HCCL_E_NOT_SUPPORT;                                          \
        }                                                                       \
    } while (0)

        if (group == ApiGroup::HCCL) {
            HCOMM_LOAD_SYMBOL(HcclChannelConfigCreate, HcommHcclChannelConfigCreateFunc, "HcclChannelConfigCreate");
            HCOMM_LOAD_SYMBOL(HcclChannelConfigDestroy, HcommHcclChannelConfigDestroyFunc, "HcclChannelConfigDestroy");
            HCOMM_LOAD_SYMBOL(HcclChannelConfigSetInt, HcommHcclChannelConfigSetIntFunc, "HcclChannelConfigSetInt");
            HCOMM_LOAD_SYMBOL(HcclChannelConfigSetStr, HcommHcclChannelConfigSetStrFunc, "HcclChannelConfigSetStr");
            HCOMM_LOAD_SYMBOL(
                HcclChannelAcquireWithConfig, HcommHcclChannelAcquireWithConfigFunc, "HcclChannelAcquireWithConfig");
            HCOMM_LOAD_SYMBOL(HcclEngineCtxCreate, HcommHcclEngineCtxCreateFunc, "HcclEngineCtxCreate");
            HCOMM_LOAD_SYMBOL(HcclEngineCtxCopy, HcommHcclEngineCtxCopyFunc, "HcclEngineCtxCopy");
            HCOMM_LOAD_SYMBOL(HcclEngineCtxDestroy, HcommHcclEngineCtxDestroyFunc, "HcclEngineCtxDestroy");
        } else {
            HCOMM_LOAD_SYMBOL(HcommChannelConfigCreate, HcommChannelConfigCreateFunc, "HcommChannelConfigCreate");
            HCOMM_LOAD_SYMBOL(HcommChannelConfigDestroy, HcommChannelConfigDestroyFunc, "HcommChannelConfigDestroy");
            HCOMM_LOAD_SYMBOL(HcommChannelConfigSetInt, HcommChannelConfigSetIntFunc, "HcommChannelConfigSetInt");
            HCOMM_LOAD_SYMBOL(
                HcommChannelCreateWithConfig, HcommChannelCreateWithConfigFunc, "HcommChannelCreateWithConfig");
            HCOMM_LOAD_SYMBOL(HcommChannelGetStatus, HcommChannelGetStatusFunc, "HcommChannelGetStatus");
            HCOMM_LOAD_SYMBOL(HcommChannelDestroy, HcommChannelDestroyFunc, "HcommChannelDestroy");
            HCOMM_LOAD_SYMBOL(HcommEngineCtxCreate, HcommEngineCtxCreateFunc, "HcommEngineCtxCreate");
            HCOMM_LOAD_SYMBOL(HcommEngineCtxCopy, HcommEngineCtxCopyFunc, "HcommEngineCtxCopy");
            HCOMM_LOAD_SYMBOL(HcommEngineCtxDestroy, HcommEngineCtxDestroyFunc, "HcommEngineCtxDestroy");
        }

#undef HCOMM_LOAD_SYMBOL

        api.group = group;
        api.referenceCount = 1U;
        return HCCL_SUCCESS;
    }

    static Api& GetApi()
    {
        static thread_local Api api;
        return api;
    }

    static void CloseLibrary(Api& api)
    {
        if (api.handle != nullptr) {
            (void)dlclose(api.handle);
        }
        api = Api{};
    }
};

#endif // IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_HOST_UTILS_H

#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_UTILS_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_UTILS_H
#endif
