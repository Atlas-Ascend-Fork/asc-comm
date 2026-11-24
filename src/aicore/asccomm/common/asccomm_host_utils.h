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
 * \file asccomm_host_utils.h
 * \brief Hcomm Host utilities
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_HOST_UTILS_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_HOST_UTILS_H
#define IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_HOST_UTILS_H

#include <dlfcn.h>
#include <stdint.h>
#include <mutex>

// Device-side multi-channel handle created by the host (asccomm_host_impl.h) and consumed by
// make_batch_handle on the device. uint64_t so it stays ABI-identical to the legacy
// typedef uint64_t MultiChannelHandle in hcomm_host.h.
typedef uint64_t multi_channel_handle;

#include "hccl/hccl_res.h"
#include "hcomm/hcomm_res_entity_defs.h"
#include "utils/log/asc_cpu_log.h"

typedef HcclResult (*HcclChannelConfigCreateFunc)(HcclChannelConfig* config);
typedef HcclResult (*HcclChannelConfigDestroyFunc)(HcclChannelConfig config);
typedef HcclResult (*HcclChannelConfigSetIntFunc)(HcclChannelConfig config, HcclChannelConfigType type, uint32_t value);
typedef HcclResult (*HcclChannelConfigSetStrFunc)(
    HcclChannelConfig config, HcclChannelConfigType type, const char* value);
typedef HcclResult (*HcclChannelAcquireWithConfigFunc)(
    HcclComm comm, CommEngine engine, const HcclChannelDesc* channel_descs, uint32_t channel_num,
    HcclChannelConfig config, ChannelHandle* channels);
typedef HcclResult (*HcclEngineCtxCreateFunc)(
    HcclComm comm, const char* ctx_tag, CommEngine engine, uint64_t size, void** ctx);
typedef HcclResult (*HcclEngineCtxCopyFunc)(
    HcclComm comm, CommEngine engine, const char* ctx_tag, const void* src_ctx, uint64_t size, uint64_t dst_ctx_offset);
typedef HcommResult (*HcommChannelConfigCreateFunc)(HcommChannelConfig* config);
typedef HcommResult (*HcommChannelConfigDestroyFunc)(HcommChannelConfig config);
typedef HcommResult (*HcommChannelConfigSetIntFunc)(
    HcommChannelConfig config, HcommChannelConfigType type, uint32_t value);
typedef HcommResult (*HcommChannelCreateWithConfigFunc)(
    EndpointHandle endpoint_handle, CommEngine engine, HcommChannelDesc* channel_descs, uint32_t channel_num,
    HcommChannelConfig config, ChannelHandle* channels);
typedef HcommResult (*HcommChannelGetStatusFunc)(
    const ChannelHandle* channel_list, uint32_t list_num, int32_t* status_list);
typedef HcommResult (*HcommChannelDestroyFunc)(const ChannelHandle* channels, uint32_t channel_num);

class dl_hcomm_api {
public:
    static HcclResult load_hccl_api() { return load_hccl_library(); }

    static HcommResult load_hcomm_api() { return load_hcomm_library(); }

    static inline HcclResult hccl_channel_config_create(HcclChannelConfig* config)
    {
        return get_hcomm_api().hccl_channel_config_create(config);
    }

    static inline HcclResult hccl_channel_config_destroy(HcclChannelConfig config)
    {
        return get_hcomm_api().hccl_channel_config_destroy(config);
    }

    static inline HcclResult hccl_channel_config_set_int(
        HcclChannelConfig config, HcclChannelConfigType type, uint32_t value)
    {
        return get_hcomm_api().hccl_channel_config_set_int(config, type, value);
    }

    static inline HcclResult hccl_channel_config_set_str(
        HcclChannelConfig config, HcclChannelConfigType type, const char* value)
    {
        return get_hcomm_api().hccl_channel_config_set_str(config, type, value);
    }

    static inline HcclResult hccl_channel_acquire_with_config(
        HcclComm comm, CommEngine engine, const HcclChannelDesc* channel_descs, uint32_t channel_num,
        HcclChannelConfig config, ChannelHandle* channels)
    {
        return get_hcomm_api().hccl_channel_acquire_with_config(
            comm, engine, channel_descs, channel_num, config, channels);
    }

    static inline HcclResult hccl_engine_ctx_create(
        HcclComm comm, const char* ctx_tag, CommEngine engine, uint64_t size, void** ctx)
    {
        return get_hcomm_api().hccl_engine_ctx_create(comm, ctx_tag, engine, size, ctx);
    }

    static inline HcclResult hccl_engine_ctx_copy(
        HcclComm comm, CommEngine engine, const char* ctx_tag, const void* src_ctx, uint64_t size,
        uint64_t dst_ctx_offset)
    {
        return get_hcomm_api().hccl_engine_ctx_copy(comm, engine, ctx_tag, src_ctx, size, dst_ctx_offset);
    }

    static inline HcommResult hcomm_channel_config_create(HcommChannelConfig* config)
    {
        return get_hcomm_api().hcomm_channel_config_create(config);
    }

    static inline HcommResult hcomm_channel_config_destroy(HcommChannelConfig config)
    {
        return get_hcomm_api().hcomm_channel_config_destroy(config);
    }

    static inline HcommResult hcomm_channel_config_set_int(
        HcommChannelConfig config, HcommChannelConfigType type, uint32_t value)
    {
        return get_hcomm_api().hcomm_channel_config_set_int(config, type, value);
    }

    static inline HcommResult hcomm_channel_create_with_config(
        EndpointHandle endpoint_handle, CommEngine engine, HcommChannelDesc* channel_descs, uint32_t channel_num,
        HcommChannelConfig config, ChannelHandle* channels)
    {
        return get_hcomm_api().hcomm_channel_create_with_config(
            endpoint_handle, engine, channel_descs, channel_num, config, channels);
    }

    static inline HcommResult hcomm_channel_get_status(
        const ChannelHandle* channel_list, uint32_t list_num, int32_t* status_list)
    {
        return get_hcomm_api().hcomm_channel_get_status(channel_list, list_num, status_list);
    }

    static inline HcommResult hcomm_channel_destroy(const ChannelHandle* channels, uint32_t channel_num)
    {
        return get_hcomm_api().hcomm_channel_destroy(channels, channel_num);
    }

private:
    struct hcomm_api {
        bool hccl_loaded = false;
        bool hcomm_loaded = false;
        HcclChannelConfigCreateFunc hccl_channel_config_create = nullptr;
        HcclChannelConfigDestroyFunc hccl_channel_config_destroy = nullptr;
        HcclChannelConfigSetIntFunc hccl_channel_config_set_int = nullptr;
        HcclChannelConfigSetStrFunc hccl_channel_config_set_str = nullptr;
        HcclChannelAcquireWithConfigFunc hccl_channel_acquire_with_config = nullptr;
        HcclEngineCtxCreateFunc hccl_engine_ctx_create = nullptr;
        HcclEngineCtxCopyFunc hccl_engine_ctx_copy = nullptr;
        HcommChannelConfigCreateFunc hcomm_channel_config_create = nullptr;
        HcommChannelConfigDestroyFunc hcomm_channel_config_destroy = nullptr;
        HcommChannelConfigSetIntFunc hcomm_channel_config_set_int = nullptr;
        HcommChannelCreateWithConfigFunc hcomm_channel_create_with_config = nullptr;
        HcommChannelGetStatusFunc hcomm_channel_get_status = nullptr;
        HcommChannelDestroyFunc hcomm_channel_destroy = nullptr;
    };

    struct hcomm_library {
        void* handle = nullptr;
        hcomm_api api;
    };

    static HcclResult load_hccl_library()
    {
        std::lock_guard<std::mutex> guard(get_mutex());
        hcomm_library& library = get_library();
        hcomm_api& api = library.api;
        if (api.hccl_loaded) {
            return HCCL_SUCCESS;
        }
        HcclResult ret = static_cast<HcclResult>(open_library(library));
        if (ret != HCCL_SUCCESS) {
            return ret;
        }

#define ASCCOMM_LOAD_SYMBOL(member, symbol)                                                    \
    do {                                                                                       \
        ret = static_cast<HcclResult>(load_symbol(library.handle, symbol, loaded_api.member)); \
        if (ret != HCCL_SUCCESS) {                                                             \
            close_library_if_unused(library);                                                  \
            return ret;                                                                        \
        }                                                                                      \
    } while (0)

        hcomm_api loaded_api = api;
        ASCCOMM_LOAD_SYMBOL(hccl_channel_config_create, "HcclChannelConfigCreate");
        ASCCOMM_LOAD_SYMBOL(hccl_channel_config_destroy, "HcclChannelConfigDestroy");
        ASCCOMM_LOAD_SYMBOL(hccl_channel_config_set_int, "HcclChannelConfigSetInt");
        ASCCOMM_LOAD_SYMBOL(hccl_channel_config_set_str, "HcclChannelConfigSetStr");
        ASCCOMM_LOAD_SYMBOL(hccl_channel_acquire_with_config, "HcclChannelAcquireWithConfig");
        ASCCOMM_LOAD_SYMBOL(hccl_engine_ctx_create, "HcclEngineCtxCreate");
        ASCCOMM_LOAD_SYMBOL(hccl_engine_ctx_copy, "HcclEngineCtxCopy");

#undef ASCCOMM_LOAD_SYMBOL

        loaded_api.hccl_loaded = true;
        api = loaded_api;
        return HCCL_SUCCESS;
    }

    static HcommResult load_hcomm_library()
    {
        std::lock_guard<std::mutex> guard(get_mutex());
        hcomm_library& library = get_library();
        hcomm_api& api = library.api;
        if (api.hcomm_loaded) {
            return HCCL_SUCCESS;
        }
        HcommResult ret = static_cast<HcommResult>(open_library(library));
        if (ret != HCCL_SUCCESS) {
            return ret;
        }

#define ASCCOMM_LOAD_SYMBOL(member, symbol)                                                     \
    do {                                                                                        \
        ret = static_cast<HcommResult>(load_symbol(library.handle, symbol, loaded_api.member)); \
        if (ret != HCCL_SUCCESS) {                                                              \
            close_library_if_unused(library);                                                   \
            return ret;                                                                         \
        }                                                                                       \
    } while (0)

        hcomm_api loaded_api = api;
        ASCCOMM_LOAD_SYMBOL(hcomm_channel_config_create, "HcommChannelConfigCreate");
        ASCCOMM_LOAD_SYMBOL(hcomm_channel_config_destroy, "HcommChannelConfigDestroy");
        ASCCOMM_LOAD_SYMBOL(hcomm_channel_config_set_int, "HcommChannelConfigSetInt");
        ASCCOMM_LOAD_SYMBOL(hcomm_channel_create_with_config, "HcommChannelCreateWithConfig");
        ASCCOMM_LOAD_SYMBOL(hcomm_channel_get_status, "HcommChannelGetStatus");
        ASCCOMM_LOAD_SYMBOL(hcomm_channel_destroy, "HcommChannelDestroy");

#undef ASCCOMM_LOAD_SYMBOL

        loaded_api.hcomm_loaded = true;
        api = loaded_api;
        return HCCL_SUCCESS;
    }

    static int32_t open_library(hcomm_library& library)
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

    template <typename func_type>
    static int32_t load_symbol(void* handle, const char* symbol, func_type& func)
    {
        (void)dlerror();
        func = reinterpret_cast<func_type>(dlsym(handle, symbol));
        const char* error = dlerror();
        if (func == nullptr || error != nullptr) {
            ASC_CPU_LOG_ERROR(
                "[ERROR] [%s] load %s failed: %s.", __func__, symbol, error == nullptr ? "symbol not found" : error);
            return HCCL_E_NOT_SUPPORT;
        }
        return HCCL_SUCCESS;
    }

    static hcomm_api& get_hcomm_api() { return get_library().api; }

    static hcomm_library& get_library()
    {
        static hcomm_library library;
        return library;
    }

    static std::mutex& get_mutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    static void close_library_if_unused(hcomm_library& library)
    {
        if (library.api.hccl_loaded || library.api.hcomm_loaded) {
            return;
        }
        if (library.handle != nullptr) {
            (void)dlclose(library.handle);
        }
        library.handle = nullptr;
    }
};

#endif // IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_HOST_UTILS_H

#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_HOST_UTILS_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_HOST_UTILS_H
#endif
