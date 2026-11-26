/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "rank_context.h"

#include <limits>
#include <set>
#include <stdexcept>

namespace asccomm_api_bench {
namespace {
uint32_t DeviceNumber(const std::string& text)
{
    const size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        throw std::invalid_argument("device list contains an empty entry");
    const size_t end = text.find_last_not_of(" \t\r\n");
    uint32_t value = 0;
    for (size_t index = begin; index <= end; ++index) {
        const char ch = text[index];
        if (ch < '0' || ch > '9')
            throw std::invalid_argument("device list requires unsigned decimal indices");
        const uint32_t digit = static_cast<uint32_t>(ch - '0');
        if (value > (std::numeric_limits<uint32_t>::max() - digit) / 10)
            throw std::invalid_argument("device index overflows uint32");
        value = value * 10 + digit;
    }
    return value;
}

void ValidateDevices(const std::vector<uint32_t>& devices, uint32_t available_devices)
{
    std::set<uint32_t> seen;
    for (uint32_t device : devices) {
        if (device >= available_devices)
            throw std::invalid_argument("device index is outside the runtime-visible device count");
        if (!seen.insert(device).second)
            throw std::invalid_argument("device list contains a duplicate index");
    }
}
} // namespace

void ValidateRankContext(const Options& options, const RankContext& context)
{
    if (options.ranks == 0 || options.devices_per_node == 0 ||
        options.ranks > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        options.devices_per_node > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        options.ranks % options.devices_per_node != 0) {
        throw std::invalid_argument("require positive MPI-sized ranks divisible by devices_per_node");
    }
    if (context.world_size != options.ranks || context.global_rank >= context.world_size)
        throw std::invalid_argument("rank context does not match the requested MPI world");
    if (context.local_size != options.devices_per_node || context.local_rank >= context.local_size)
        throw std::invalid_argument("node-local MPI size/rank does not match devices_per_node");
    if (context.node_count != options.ranks / options.devices_per_node || context.node_index >= context.node_count)
        throw std::invalid_argument("rank context has an unexpected node count/index");
    if (context.global_rank != context.node_index * context.local_size + context.local_rank)
        throw std::invalid_argument("MPI placement must assign contiguous global ranks to each node");
    if (context.hostname.empty())
        throw std::invalid_argument("MPI processor name is empty");
    for (unsigned char ch : context.hostname) {
        if (ch < 32 || ch == 127)
            throw std::invalid_argument("MPI processor name contains a control character");
    }
}

std::vector<uint32_t> ParseDeviceList(const std::string& text, uint32_t available_devices)
{
    std::vector<uint32_t> devices;
    if (text.empty())
        return devices;
    size_t begin = 0;
    while (begin <= text.size()) {
        const size_t end = text.find(',', begin);
        devices.push_back(DeviceNumber(text.substr(begin, end == std::string::npos ? end : end - begin)));
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    ValidateDevices(devices, available_devices);
    return devices;
}

uint32_t SelectLocalDevice(
    uint32_t local_rank, uint32_t local_size, uint32_t available_devices, const std::vector<uint32_t>& devices)
{
    if (local_size == 0 || local_rank >= local_size || available_devices == 0)
        throw std::invalid_argument("invalid local rank/size or no runtime-visible devices");
    if (devices.empty()) {
        if (local_size > available_devices)
            throw std::invalid_argument("node-local rank count exceeds runtime-visible devices");
        return local_rank;
    }
    if (devices.size() != local_size)
        throw std::invalid_argument("explicit device list must have one index per node-local rank");
    ValidateDevices(devices, available_devices);
    return devices[local_rank];
}

} // namespace asccomm_api_bench
