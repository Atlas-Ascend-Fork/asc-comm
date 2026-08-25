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

typedef HcclResult (*HcommHcclChannelConfigCreateFunc)(HcclChannelConfig* config);
typedef HcclResult (*HcommHcclChannelConfigDestroyFunc)(HcclChannelConfig config);
typedef HcclResult (*HcommHcclChannelConfigSetIntFunc)(
    HcclChannelConfig config, HcclChannelConfigType type, uint32_t value);
typedef HcclResult (*HcommHcclChannelConfigSetStrFunc)(
    HcclChannelConfig config, HcclChannelConfigType type, const char* value);
typedef HcclResult (*HcommHcclChannelAcquireWithConfigFunc)(
    HcclComm comm, CommEngine engine, const HcclChannelDesc* channelDescs, uint32_t channelNum,
    HcclChannelConfig config, ChannelHandle* channels);
typedef HcclResult (*HcommHcclEngineCtxCreateFunc)(
    HcclComm comm, const char* ctxTag, CommEngine engine, uint64_t size, void** ctx);
typedef HcclResult (*HcommHcclEngineCtxCopyFunc)(
    HcclComm comm, CommEngine engine, const char* ctxTag, const void* srcCtx, uint64_t size, uint64_t dstCtxOffset);
typedef HcclResult (*HcommHcclEngineCtxDestroyFunc)(HcclComm comm, const char* ctxTag, CommEngine engine);

class DlHcclApi {
public:
    static HcclResult LoadLibrary()
    {
        HcclApi& api = GetApi();
        if (api.referenceCount != 0U) {
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

#define HCOMM_LOAD_HCCL_SYMBOL(member, type, symbol)                            \
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

        HCOMM_LOAD_HCCL_SYMBOL(HcclChannelConfigCreate, HcommHcclChannelConfigCreateFunc, "HcclChannelConfigCreate");
        HCOMM_LOAD_HCCL_SYMBOL(HcclChannelConfigDestroy, HcommHcclChannelConfigDestroyFunc, "HcclChannelConfigDestroy");
        HCOMM_LOAD_HCCL_SYMBOL(HcclChannelConfigSetInt, HcommHcclChannelConfigSetIntFunc, "HcclChannelConfigSetInt");
        HCOMM_LOAD_HCCL_SYMBOL(HcclChannelConfigSetStr, HcommHcclChannelConfigSetStrFunc, "HcclChannelConfigSetStr");
        HCOMM_LOAD_HCCL_SYMBOL(
            HcclChannelAcquireWithConfig, HcommHcclChannelAcquireWithConfigFunc, "HcclChannelAcquireWithConfig");
        HCOMM_LOAD_HCCL_SYMBOL(HcclEngineCtxCreate, HcommHcclEngineCtxCreateFunc, "HcclEngineCtxCreate");
        HCOMM_LOAD_HCCL_SYMBOL(HcclEngineCtxCopy, HcommHcclEngineCtxCopyFunc, "HcclEngineCtxCopy");
        HCOMM_LOAD_HCCL_SYMBOL(HcclEngineCtxDestroy, HcommHcclEngineCtxDestroyFunc, "HcclEngineCtxDestroy");

#undef HCOMM_LOAD_HCCL_SYMBOL

        api.referenceCount = 1U;
        return HCCL_SUCCESS;
    }

    static void CleanupLibrary()
    {
        HcclApi& api = GetApi();
        if (api.referenceCount == 0U) {
            return;
        }
        --api.referenceCount;
        if (api.referenceCount == 0U) {
            CloseLibrary(api);
        }
    }

    static inline HcclResult HcclChannelConfigCreate(HcclChannelConfig* config)
    {
        return GetApi().HcclChannelConfigCreate(config);
    }

    static inline HcclResult HcclChannelConfigDestroy(HcclChannelConfig config)
    {
        return GetApi().HcclChannelConfigDestroy(config);
    }

    static inline HcclResult HcclChannelConfigSetInt(
        HcclChannelConfig config, HcclChannelConfigType type, uint32_t value)
    {
        return GetApi().HcclChannelConfigSetInt(config, type, value);
    }

    static inline HcclResult HcclChannelConfigSetStr(
        HcclChannelConfig config, HcclChannelConfigType type, const char* value)
    {
        return GetApi().HcclChannelConfigSetStr(config, type, value);
    }

    static inline HcclResult HcclChannelAcquireWithConfig(
        HcclComm comm, CommEngine engine, const HcclChannelDesc* channelDescs, uint32_t channelNum,
        HcclChannelConfig config, ChannelHandle* channels)
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

private:
    struct HcclApi {
        void* handle = nullptr;
        uint32_t referenceCount = 0U;
        HcommHcclChannelConfigCreateFunc HcclChannelConfigCreate = nullptr;
        HcommHcclChannelConfigDestroyFunc HcclChannelConfigDestroy = nullptr;
        HcommHcclChannelConfigSetIntFunc HcclChannelConfigSetInt = nullptr;
        HcommHcclChannelConfigSetStrFunc HcclChannelConfigSetStr = nullptr;
        HcommHcclChannelAcquireWithConfigFunc HcclChannelAcquireWithConfig = nullptr;
        HcommHcclEngineCtxCreateFunc HcclEngineCtxCreate = nullptr;
        HcommHcclEngineCtxCopyFunc HcclEngineCtxCopy = nullptr;
        HcommHcclEngineCtxDestroyFunc HcclEngineCtxDestroy = nullptr;
    };

    static HcclApi& GetApi()
    {
        static thread_local HcclApi api;
        return api;
    }

    static void CloseLibrary(HcclApi& api)
    {
        if (api.handle != nullptr) {
            (void)dlclose(api.handle);
        }
        api = HcclApi{};
    }
};

#endif // IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_HOST_UTILS_H

#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_UTILS_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_UTILS_H
#endif
