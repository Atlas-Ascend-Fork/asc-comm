/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "profiling/aprof_pub.h"
#include "runtime/rt_external_base.h"

namespace {

struct FakeTypeInfo {
    uint16_t level = 0;
    uint32_t type = 0;
    std::string name;
};

struct FakeApiReport {
    uint16_t level = 0;
    uint32_t type = 0;
    uint32_t threadId = 0;
    uint64_t beginTime = 0;
    uint64_t endTime = 0;
    uint64_t itemId = 0;
};

struct FakeAdditionalReport {
    uint16_t level = 0;
    uint32_t type = 0;
    uint32_t threadId = 0;
    uint32_t dataLen = 0;
    uint64_t timeStamp = 0;
    std::vector<uint8_t> data;
};

ProfCommandHandle g_callback = nullptr;
uint32_t g_registeredModuleId = 0xFFFFFFFFU;
uint32_t g_registerCallbackCount = 0;
uint64_t g_cycleTime = 0;
std::string g_mode = "normal";
std::vector<FakeTypeInfo> g_typeInfos;
std::vector<FakeApiReport> g_apiReports;
std::vector<FakeAdditionalReport> g_additionalReports;

bool failedMode() { return g_mode == "fail_register" || g_mode == "fail_report"; }

uint64_t hashString(const char* text)
{
    uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(text); *cursor != 0; ++cursor) {
        hash ^= *cursor;
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

extern "C" {

void ResetFakeProfapi()
{
    g_callback = nullptr;
    g_registeredModuleId = 0xFFFFFFFFU;
    g_registerCallbackCount = 0;
    g_cycleTime = 0;
    g_mode = "normal";
    g_typeInfos.clear();
    g_apiReports.clear();
    g_additionalReports.clear();
}

void SetFakeProfapiMode(const char* mode) { g_mode = mode == nullptr ? "normal" : mode; }

int32_t StartFakeProfiling(uint64_t profSwitch)
{
    if (g_callback == nullptr)
        return -1;
    rtProfCommandHandle_t command{};
    command.profSwitch = profSwitch;
    command.type = PROF_COMMANDHANDLE_TYPE_START;
    return g_callback(RT_PROF_CTRL_SWITCH, &command, sizeof(command));
}

int32_t StopFakeProfiling()
{
    if (g_callback == nullptr)
        return -1;
    rtProfCommandHandle_t command{};
    command.type = PROF_COMMANDHANDLE_TYPE_STOP;
    return g_callback(RT_PROF_CTRL_SWITCH, &command, sizeof(command));
}

uint32_t FakeProfapiRegisteredModuleId() { return g_registeredModuleId; }

uint32_t FakeProfapiRegisterCallbackCount() { return g_registerCallbackCount; }

uint32_t FakeProfapiTypeInfoCount() { return static_cast<uint32_t>(g_typeInfos.size()); }

int32_t FakeProfapiTypeInfo(uint32_t index, uint16_t* level, uint32_t* type, const char** name)
{
    if (index >= g_typeInfos.size() || level == nullptr || type == nullptr || name == nullptr)
        return -1;
    const FakeTypeInfo& info = g_typeInfos[index];
    *level = info.level;
    *type = info.type;
    *name = info.name.c_str();
    return 0;
}

uint32_t FakeProfapiApiReportCount() { return static_cast<uint32_t>(g_apiReports.size()); }

int32_t FakeProfapiApiReport(
    uint32_t index, uint16_t* level, uint32_t* type, uint32_t* threadId, uint64_t* beginTime, uint64_t* endTime,
    uint64_t* itemId)
{
    if (index >= g_apiReports.size() || level == nullptr || type == nullptr || threadId == nullptr ||
        beginTime == nullptr || endTime == nullptr || itemId == nullptr)
        return -1;
    const FakeApiReport& report = g_apiReports[index];
    *level = report.level;
    *threadId = report.threadId;
    *type = report.type;
    *beginTime = report.beginTime;
    *endTime = report.endTime;
    *itemId = report.itemId;
    return 0;
}

uint32_t FakeProfapiAdditionalReportCount() { return static_cast<uint32_t>(g_additionalReports.size()); }

int32_t FakeProfapiAdditionalReport(
    uint32_t index, uint16_t* level, uint32_t* type, uint32_t* threadId, uint32_t* dataLen, uint64_t* timeStamp,
    uint8_t* data, uint32_t dataCapacity)
{
    if (index >= g_additionalReports.size() || level == nullptr || type == nullptr || threadId == nullptr ||
        dataLen == nullptr || timeStamp == nullptr || data == nullptr)
        return -1;
    const FakeAdditionalReport& report = g_additionalReports[index];
    if (report.data.size() > dataCapacity)
        return -1;
    *level = report.level;
    *type = report.type;
    *threadId = report.threadId;
    *dataLen = report.dataLen;
    *timeStamp = report.timeStamp;
    if (!report.data.empty())
        std::memcpy(data, report.data.data(), report.data.size());
    return 0;
}

uint64_t FakeProfapiHash(const char* text) { return hashString(text); }

int32_t MsprofRegisterCallback(uint32_t moduleId, ProfCommandHandle handle)
{
    ++g_registerCallbackCount;
    if (failedMode())
        return 1;
    g_registeredModuleId = moduleId;
    g_callback = handle;
    return 0;
}

int32_t MsprofRegTypeInfo(uint16_t level, uint32_t typeId, const char* typeName)
{
    if (failedMode())
        return 1;
    g_typeInfos.push_back(FakeTypeInfo{level, typeId, typeName == nullptr ? "" : typeName});
    return 0;
}

int32_t MsprofReportAdditionalInfo(uint32_t, VOID_PTR reportData, uint32_t)
{
    const auto* data = static_cast<const MsprofAdditionalInfo*>(reportData);
    if (data == nullptr)
        return 1;
    FakeAdditionalReport report;
    report.level = data->level;
    report.type = data->type;
    report.threadId = data->threadId;
    report.dataLen = data->dataLen;
    report.timeStamp = data->timeStamp;
    report.data.assign(data->data, data->data + data->dataLen);
    g_additionalReports.push_back(std::move(report));
    return g_mode == "fail_report" ? 1 : 0;
}

int32_t MsprofReportApi(uint32_t, const MsprofApi* data)
{
    if (data == nullptr)
        return 1;
    g_apiReports.push_back(
        FakeApiReport{data->level, data->type, data->threadId, data->beginTime, data->endTime, data->itemId});
    return g_mode == "fail_report" ? 1 : 0;
}

uint64_t MsprofStr2Id(const char* hashInfo, size_t length)
{
    if (hashInfo == nullptr)
        return 0xFFFFFFFFFFFFFFFFULL;
    return hashString(std::string(hashInfo, length).c_str());
}

uint64_t MsprofSysCycleTime()
{
    g_cycleTime += 1000;
    return g_cycleTime;
}

} // extern "C"
