/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "comm_bootstrap.h"

#include <stdexcept>
#include <utility>

#include "options.h"

namespace asccomm_api_bench {
namespace {
constexpr size_t MAX_ROOT_INFO_BYTES = 1024 * 1024;

// HcclCommConfig uses Ascend950 accelerator values, not the HcclOpExpansionMode
// query enum. Keep this boundary separate from MC2 query/tiling codes even
// where their current numeric values coincide.
uint8_t HcclCommExpansionModeCode(Engine engine)
{
    switch (engine) {
        case Engine::AicpuTs:
            return 2;
        case Engine::CcuSched:
            return 6;
        case Engine::CcuMs:
            return 5;
    }
    throw std::invalid_argument("invalid engine");
}

CommBootstrapResult Failure(CommBootstrapResult result, const char* stage, const std::string& local_error, int64_t ret)
{
    result.failure_stage = stage;
    result.error = local_error.empty() ? std::string(stage) + " failed on another rank" : local_error;
    result.ret = ret;
    result.cleanup_ret = result.communicator.Close();
    return result;
}

std::string ApiError(const char* operation, int64_t ret)
{
    return std::string(operation) + " failed, ret=" + std::to_string(ret);
}
} // namespace

CommHandle::CommHandle(void* handle, std::function<int64_t(void*)> destroy)
    : handle_(handle), destroy_(std::move(destroy))
{}

CommHandle::~CommHandle() { Close(); }

CommHandle::CommHandle(CommHandle&& other) noexcept
    : handle_(other.handle_), destroy_(std::move(other.destroy_)), name_(std::move(other.name_))
{
    other.handle_ = nullptr;
}

CommHandle& CommHandle::operator=(CommHandle&& other) noexcept
{
    if (this != &other) {
        Close();
        handle_ = other.handle_;
        destroy_ = std::move(other.destroy_);
        name_ = std::move(other.name_);
        other.handle_ = nullptr;
    }
    return *this;
}

void CommHandle::SetName(std::string name) { name_ = std::move(name); }

int64_t CommHandle::Close() noexcept
{
    if (handle_ == nullptr)
        return 0;
    void* handle = handle_;
    handle_ = nullptr;
    name_.clear();
    try {
        return destroy_ ? destroy_(handle) : -1;
    } catch (...) {
        return -1;
    }
}

CommBootstrapResult BootstrapCommunicator(const Options& options, Coordinator& coordinator, const CommBootstrapApi& api)
{
    CommBootstrapResult result;
    std::string local_error;
    uint8_t expansion_mode = 0;
    std::function<int64_t(void*)> destroy;
    try {
        ValidateOptions(options);
        if (options.ranks != coordinator.Size() || coordinator.Rank() >= coordinator.Size())
            throw std::invalid_argument("communicator rank/world does not match -n");
        if (api.root_info_bytes == 0 || api.root_info_bytes > MAX_ROOT_INFO_BYTES || !api.get_root_info ||
            !api.create || !api.get_name || !api.destroy) {
            throw std::invalid_argument(
                "complete communication bootstrap callbacks and bounded root size are required");
        }
        expansion_mode = HcclCommExpansionModeCode(options.engine);
        destroy = api.destroy;
    } catch (const std::exception& error) {
        local_error = error.what();
    }
    if (!coordinator.AllTrue(local_error.empty()))
        return Failure(std::move(result), "comm_parameters", local_error, 0);

    const std::string descriptor = std::to_string(options.ranks) + ":" + std::to_string(expansion_mode) + ":" +
                                   std::to_string(api.root_info_bytes);
    const std::string root_descriptor = coordinator.Broadcast(descriptor);
    if (!coordinator.AllTrue(descriptor == root_descriptor))
        return Failure(std::move(result), "comm_agreement", "communication world, engine or root size differs", 0);

    std::string root_info;
    int64_t local_ret = 0;
    if (coordinator.Rank() == 0) {
        try {
            local_ret = api.get_root_info(root_info);
            if (local_ret != 0)
                local_error = ApiError("get_root_info", local_ret);
            else if (root_info.size() != api.root_info_bytes)
                local_error = "get_root_info returned an unexpected binary size";
        } catch (const std::exception& error) {
            local_error = error.what();
        } catch (...) {
            local_error = "get_root_info threw a non-standard exception";
        }
    }
    if (!coordinator.AllTrue(local_error.empty()))
        return Failure(std::move(result), "comm_root_info", local_error, local_ret);

    root_info = coordinator.Broadcast(root_info);
    if (!coordinator.AllTrue(root_info.size() == api.root_info_bytes))
        return Failure(std::move(result), "comm_root_broadcast", "root-info broadcast has an unexpected size", 0);

    void* raw_handle = nullptr;
    try {
        local_ret = api.create(options.ranks, root_info, coordinator.Rank(), expansion_mode, &raw_handle);
        if (local_ret != 0)
            local_error = ApiError("create", local_ret);
        else if (raw_handle == nullptr)
            local_error = "create succeeded without a communication handle";
    } catch (const std::exception& error) {
        local_error = error.what();
    } catch (...) {
        local_error = "create threw a non-standard exception";
    }
    result.communicator = CommHandle(raw_handle, std::move(destroy));
    if (!coordinator.AllTrue(local_error.empty()))
        return Failure(std::move(result), "comm_create", local_error, local_ret);

    std::string name;
    try {
        local_ret = api.get_name(result.communicator.Get(), name);
        if (local_ret != 0)
            local_error = ApiError("get_name", local_ret);
        else if (name.empty() || name.find('\0') != std::string::npos)
            local_error = "get_name returned an empty or invalid communication name";
    } catch (const std::exception& error) {
        local_error = error.what();
    } catch (...) {
        local_error = "get_name threw a non-standard exception";
    }
    if (!coordinator.AllTrue(local_error.empty()))
        return Failure(std::move(result), "comm_name", local_error, local_ret);
    result.communicator.SetName(std::move(name));
    return result;
}

} // namespace asccomm_api_bench
