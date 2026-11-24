/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ccu_profiling.h"
#include "ccu_runtime_external.h"
#include "ccu_runtime_backend.h"
#include "profiling/aprof_pub.h"

extern "C" {
void ResetFakeProfapi();
void SetFakeProfapiMode(const char* mode);
int32_t StartFakeProfiling(uint64_t profSwitch);
int32_t StopFakeProfiling();
uint32_t FakeProfapiRegisteredModuleId();
uint32_t FakeProfapiRegisterCallbackCount();
uint32_t FakeProfapiTypeInfoCount();
int32_t FakeProfapiTypeInfo(uint32_t index, uint16_t* level, uint32_t* type, const char** name);
uint32_t FakeProfapiApiReportCount();
int32_t FakeProfapiApiReport(
    uint32_t index, uint16_t* level, uint32_t* type, uint32_t* threadId, uint64_t* beginTime, uint64_t* endTime,
    uint64_t* itemId);
uint32_t FakeProfapiAdditionalReportCount();
int32_t FakeProfapiAdditionalReport(
    uint32_t index, uint16_t* level, uint32_t* type, uint32_t* threadId, uint32_t* dataLen, uint64_t* timeStamp,
    uint8_t* data, uint32_t dataCapacity);
uint64_t FakeProfapiHash(const char* text);
ccu_status_t ccu_compiler_submit(
    ccu_context_t* context, const ccu_kernel_descriptor_t* descriptor, void* stream, const uint64_t* arguments,
    uint32_t argumentCount, const void* externalArguments, uint32_t externalArgumentCount);
}

namespace {

uint64_t readU64(const uint8_t* data, uint32_t offset)
{
    uint64_t value = 0;
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
}

uint32_t readU32(const uint8_t* data, uint32_t offset)
{
    uint32_t value = 0;
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
}

uint16_t readU16(const uint8_t* data, uint32_t offset)
{
    uint16_t value = 0;
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
}

class TestBackend final : public ccu::runtime::Backend {
public:
    ccu_status_t contextCreate(void*, void** context, uint32_t* rank, uint32_t* rankSize) noexcept override
    {
        *context = this;
        *rank = 0;
        *rankSize = 2;
        return CCU_STATUS_SUCCESS;
    }

    ccu_status_t contextDestroy(void*) noexcept override { return CCU_STATUS_SUCCESS; }

    ccu_status_t externalEventVa(void*, uint32_t, void**, uint64_t*) noexcept override
    {
        return CCU_STATUS_INVALID_ARGUMENT;
    }

    ccu_status_t externalXnVa(void*, uint32_t, void**, uint64_t*) noexcept override
    {
        return CCU_STATUS_INVALID_ARGUMENT;
    }

    ccu_status_t launch(
        void*, const ccu_kernel_descriptor_t*, uint32_t, void*, HcomCcuSchd, const uint64_t*, uint32_t,
        const ccu::runtime::BackendExternalArg*, uint32_t, void** backendLaunch) noexcept override
    {
        ++launchCount;
        *backendLaunch = this;
        return CCU_STATUS_SUCCESS;
    }

    ccu::runtime::BackendSyncResult synchronize(void*, uint64_t, uint64_t) noexcept override
    {
        return {CCU_STATUS_SUCCESS, ccu::runtime::CCU_ABORT_NONE, true};
    }

    ccu_status_t launchDestroy(void*) noexcept override { return CCU_STATUS_SUCCESS; }

    uint32_t launchCount = 0;
};

int base64Value(char character)
{
    if (character >= 'A' && character <= 'Z')
        return character - 'A';
    if (character >= 'a' && character <= 'z')
        return character - 'a' + 26;
    if (character >= '0' && character <= '9')
        return character - '0' + 52;
    if (character == '+')
        return 62;
    if (character == '/')
        return 63;
    return -1;
}

std::vector<uint8_t> decodeBase64(const char* text)
{
    std::vector<uint8_t> bytes;
    uint32_t buffer = 0;
    uint32_t bits = 0;
    for (size_t i = 0; text[i] != '\0'; ++i) {
        const int value = base64Value(text[i]);
        if (value < 0)
            continue;
        buffer = (buffer << 6U) | static_cast<uint32_t>(value);
        bits += 6U;
        if (bits >= 8U) {
            bits -= 8U;
            bytes.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFFU));
        }
    }
    return bytes;
}

// CI rejects binary files under tests, so embed the fixture as base64 text.
std::vector<uint8_t> loadFixtureObject()
{
    static constexpr char encodedObject[] =
        "Q0NVT0JKAAABAAAABAAAAAAAAAAAAAAA+FNKu7Fgsa8IAQAAAAAAAAwAAAAAAAAAQAAAAAAAAAAHAAAAAAAAAAEAAAAAAAAA"
        "6AAAAAAAAACQAAAAAAAAAAIAAAAAAAAAeAEAAAAAAACAAQAAAAAAAAMAAAAAAAAA+AIAAAAAAADAAAAAAAAAAAQAAAAAAAAA"
        "uAMAAAAAAAAAAAAAAAAAAAUAAAAAAAAAuAMAAAAAAACQAAAAAAAAAAYAAAAAAAAASAQAAAAAAAAUAQAAAAAAAAgAAAAAAAAA"
        "XAUAAAAAAAC4AAAAAAAAAAAAAAAAAAAALAAAAAAAAAAAAAAAAAAAACwAAAAAAAAALAAAAAAAAAABAAAAAAAAAFgAAAAAAAAA"
        "LAAAAAAAAAACAAAAAAAAAIQAAAAAAAAALAAAAAAAAAADAAAAAAAAALAAAAAAAAAALAAAAAAAAAAEAAAAAAAAANwAAAAAAAAA"
        "LAAAAAAAAAAFAAAAAAAAAAAAAAAAAAAAIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAYAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAEAAAAgAAAAAAAAACAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABgAAAAAAAAAGAAAAAAAAAAAAAAAAAAAAAAAAAABAAAA"
        "QAAAAAAAAAAgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAwAAAAAAAAABgAAAAAAAAAAAAAAAAAAAAAAAAAAQAAAGAAAAAAAAAA"
        "IAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAASAAAAAAAAAAYAAAAAAAAAAAAAAAAAAAAAAAAAAEAAACAAAAAAAAAACAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAGAAAAAAAAAAGAAAAAAAAAAAAAAAAAAAAAAAAAABAAAAoAAAAAAAAAAgAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAB4AAAAAAAAABgAAAAAAAAAAAAAAAAAAAAAAAAAAQAAAAMAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AwAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAMAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAwAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAEAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAEAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAG1pbmltYWxfX3UzMl8yX19lX19fY2N1X3JlZHVjZV9vcF90XzBfX3JhbmswbWluaW1hbF9fdTMy"
        "XzJfX2VfX19jY3VfcmVkdWNlX29wX3RfMF9fcmFuazFtaW5pbWFsX191MzJfNF9fZV9fX2NjdV9yZWR1Y2Vfb3BfdF8yX19y"
        "YW5rMG1pbmltYWxfX3UzMl80X19lX19fY2N1X3JlZHVjZV9vcF90XzJfX3JhbmsxbWluaW1hbF9fdTMyXzRfX2VfX19jY3Vf"
        "cmVkdWNlX29wX3RfMl9fcmFuazJtaW5pbWFsX191MzJfNF9fZV9fX2NjdV9yZWR1Y2Vfb3BfdF8yX19yYW5rM2NjdWNfbWlu"
        "aW1hbAYAAAAMAAAABgAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAQAAAAAAAAABAAAAAAAAAAIAAAAAAAAAAQAAAAAAAAADAAAA"
        "AAAAAAEAAAAAAAAABAAAAAAAAAABAAAAAAAAAAUAAAAAAAAAAQAAAAAAAAAAAAAAAgAAAAAAAAAAAAAAAgAAAAAAAAAAAAAA"
        "AgAAAAAAAAAAAAAAAgAAAAAAAAAAAAAAAgAAAAAAAAAAAAAAAgAAAAAAAAA=";

    return decodeBase64(encodedObject);
}

} // namespace

namespace ccu::runtime {
Backend& hcommBackend() noexcept
{
    static TestBackend backend;
    return backend;
}
} // namespace ccu::runtime

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(CcuRuntimeProfiling, Phase1LaunchLifecycle)
{
    ASSERT_EQ(::setenv("CCU_FAKE_TASK_QUERY_MODE", "success", 1), 0);
    ResetFakeProfapi();
    SetFakeProfapiMode("normal");

    EXPECT_FALSE(ccu::runtime::profiling::isReportReady());
    EXPECT_EQ(FakeProfapiRegisterCallbackCount(), 1U);
    EXPECT_EQ(FakeProfapiRegisteredModuleId(), 3U);

    ASSERT_EQ(StartFakeProfiling(0x2), 0);
    EXPECT_TRUE(ccu::runtime::profiling::isReportReady());
    ASSERT_EQ(FakeProfapiTypeInfoCount(), 1U);
    uint16_t level = 0;
    uint32_t type = 0;
    const char* typeName = nullptr;
    ASSERT_EQ(FakeProfapiTypeInfo(0, &level, &type, &typeName), 0);
    EXPECT_EQ(level, 5500U);
    EXPECT_EQ(type, 14U);
    EXPECT_STREQ(typeName, "ccu_task_info");

    ccu::runtime::profiling::LaunchContext context{};
    const char* kernelName = "ccu::write_remote(int, float)";
    std::memcpy(context.kernelName, kernelName, std::strlen(kernelName) + 1);
    context.rankId = 2;
    context.rankSize = 4;
    context.beginTime = 101;
    context.endTime = 202;
    ASSERT_EQ(::setenv("CCU_FAKE_TASK_QUERY_MODE", "fail", 1), 0);
    ccu::runtime::profiling::reportLaunchAfterSubmit(context);
    ASSERT_EQ(FakeProfapiApiReportCount(), 1U);
    EXPECT_EQ(FakeProfapiAdditionalReportCount(), 0U);

    ASSERT_EQ(::setenv("CCU_FAKE_TASK_QUERY_MODE", "success", 1), 0);
    ccu::runtime::profiling::reportLaunchAfterSubmit(context);
    ASSERT_EQ(FakeProfapiApiReportCount(), 2U);
    uint32_t threadId = 0;
    uint64_t beginTime = 0;
    uint64_t endTime = 0;
    uint64_t itemId = 0;
    ASSERT_EQ(FakeProfapiApiReport(1, &level, &type, &threadId, &beginTime, &endTime, &itemId), 0);
    EXPECT_EQ(level, 10000U);
    EXPECT_EQ(type, 5U);
    EXPECT_GT(threadId, 0U);
    EXPECT_EQ(beginTime, context.beginTime);
    EXPECT_EQ(endTime, context.endTime);
    EXPECT_EQ(itemId, FakeProfapiHash("write_remote"));

    ASSERT_EQ(FakeProfapiAdditionalReportCount(), 1U);
    uint32_t dataLen = 0;
    uint64_t timeStamp = 0;
    uint8_t payload[MSPROF_ADDTIONAL_INFO_DATA_LENGTH] = {};
    ASSERT_EQ(
        FakeProfapiAdditionalReport(0, &level, &type, &threadId, &dataLen, &timeStamp, payload, sizeof(payload)), 0);
    EXPECT_EQ(level, 5500U);
    EXPECT_EQ(type, 14U);
    EXPECT_EQ(dataLen, 48U);
    EXPECT_EQ(timeStamp, context.endTime);
    EXPECT_EQ(payload[0], 0);
    EXPECT_EQ(payload[1], 1);
    EXPECT_EQ(readU64(payload, 8), itemId);
    EXPECT_EQ(readU64(payload, 16), 0xFFFFFFFFFFFFFFFFULL);
    EXPECT_EQ(readU32(payload, 24), context.rankId);
    EXPECT_EQ(readU32(payload, 28), context.rankSize);
    EXPECT_EQ(readU16(payload, 32), 888U);
    EXPECT_EQ(readU32(payload, 36), 777U);
    EXPECT_EQ(payload[40], 0xFF);
    EXPECT_EQ(payload[41], 0xFF);
    EXPECT_EQ(readU16(payload, 42), 0xFFFFU);

    ASSERT_EQ(StopFakeProfiling(), 0);
    EXPECT_FALSE(ccu::runtime::profiling::isReportReady());
    ccu::runtime::profiling::reportLaunchAfterSubmit(context);
    EXPECT_EQ(FakeProfapiApiReportCount(), 2U);
    EXPECT_EQ(FakeProfapiAdditionalReportCount(), 1U);

    ASSERT_EQ(StartFakeProfiling(0x2), 0);
    SetFakeProfapiMode("fail_report");
    ccu::runtime::profiling::reportLaunchAfterSubmit(context);
    SetFakeProfapiMode("normal");
    EXPECT_EQ(FakeProfapiApiReportCount(), 3U);
    EXPECT_EQ(FakeProfapiAdditionalReportCount(), 2U);
    ASSERT_EQ(::unsetenv("CCU_FAKE_TASK_QUERY_MODE"), 0);
}

TEST(CcuRuntimeProfiling, PublicSubmitReportsRealObjectKernelName)
{
    ASSERT_EQ(::setenv("CCU_FAKE_TASK_QUERY_MODE", "success", 1), 0);
    SetFakeProfapiMode("normal");
    ASSERT_EQ(StopFakeProfiling(), 0);
    const uint32_t apiBefore = FakeProfapiApiReportCount();
    ASSERT_EQ(StartFakeProfiling(0x2), 0);

    const std::vector<uint8_t> object = loadFixtureObject();
    ASSERT_EQ(object.size(), 1556U);
    const uint32_t kernelIndices[] = {1, 2};
    const ccu_image_descriptor_t image{"dav-3510", object.data(), object.size()};
    const ccu_kernel_descriptor_t descriptor{&image, 2, kernelIndices, 0};
    int stream = 0;

    TestBackend backend;
    ccu::runtime::setBackendForTesting(&backend);
    ccu_context_options_t options{};
    options.struct_size = sizeof(options);
    options.default_sync_timeout_ms = 30000;
    options.abort_timeout_ms = 10000;
    ccu_context_t* context = nullptr;
    ASSERT_EQ(ccu_context_create(&options, &context), CCU_STATUS_SUCCESS);

    const uint32_t taskBefore = FakeProfapiAdditionalReportCount();
    ASSERT_EQ(StartFakeProfiling(0x2), 0);
    ASSERT_EQ(ccu_compiler_submit(context, &descriptor, &stream, nullptr, 0, nullptr, 0), CCU_STATUS_SUCCESS);
    EXPECT_EQ(backend.launchCount, 1U);
    ASSERT_EQ(FakeProfapiApiReportCount(), apiBefore + 1);

    uint16_t level = 0;
    uint32_t type = 0;
    uint32_t threadId = 0;
    uint64_t beginTime = 0;
    uint64_t endTime = 0;
    uint64_t itemId = 0;
    ASSERT_EQ(FakeProfapiApiReport(apiBefore, &level, &type, &threadId, &beginTime, &endTime, &itemId), 0);
    EXPECT_EQ(itemId, FakeProfapiHash("minimal__u32_2__e___ccu_reduce_op_t_0__rank1"));
    EXPECT_GT(endTime, 0U);
    EXPECT_GE(endTime, beginTime);

    ASSERT_EQ(FakeProfapiAdditionalReportCount(), taskBefore + 1);
    uint32_t dataLen = 0;
    uint64_t timeStamp = 0;
    uint8_t payload[MSPROF_ADDTIONAL_INFO_DATA_LENGTH] = {};
    ASSERT_EQ(
        FakeProfapiAdditionalReport(
            taskBefore, &level, &type, &threadId, &dataLen, &timeStamp, payload, sizeof(payload)),
        0);
    EXPECT_EQ(readU64(payload, 8), itemId);
    EXPECT_EQ(readU32(payload, 24), 0U);
    EXPECT_EQ(readU32(payload, 28), 2U);
    EXPECT_EQ(readU16(payload, 32), 888U);
    EXPECT_EQ(readU32(payload, 36), 777U);
    ASSERT_EQ(ccu_synchronize(reinterpret_cast<ccu_instance_t>(context), &stream, 0), CCU_STATUS_SUCCESS);

    SetFakeProfapiMode("fail_report");
    ASSERT_EQ(ccu_compiler_submit(context, &descriptor, &stream, nullptr, 0, nullptr, 0), CCU_STATUS_SUCCESS);
    EXPECT_EQ(backend.launchCount, 2U);
    ASSERT_EQ(ccu_synchronize(reinterpret_cast<ccu_instance_t>(context), &stream, 0), CCU_STATUS_SUCCESS);

    SetFakeProfapiMode("normal");
    ASSERT_EQ(StopFakeProfiling(), 0);
    ASSERT_EQ(ccu_context_destroy(&context), CCU_STATUS_SUCCESS);
    ccu::runtime::setBackendForTesting(nullptr);
    ASSERT_EQ(::unsetenv("CCU_FAKE_TASK_QUERY_MODE"), 0);
}

TEST(CcuRuntimeProfiling, PublicSubmitFallsBackWhenKernelNameIsAbsent)
{
    std::vector<uint8_t> object = loadFixtureObject();
    ASSERT_EQ(object.size(), 1556U);
    // Mark the optional symbol table as an unknown vendor section. This
    // remains a valid .ccuobj and reproduces a null kernel-name view.
    uint64_t sectionTableOffset = 0;
    uint32_t sectionCount = 0;
    std::memcpy(&sectionTableOffset, object.data() + 48, sizeof(sectionTableOffset));
    std::memcpy(&sectionCount, object.data() + 56, sizeof(sectionCount));
    for (uint32_t i = 0; i < sectionCount; ++i) {
        const size_t entry = static_cast<size_t>(sectionTableOffset + i * 24);
        uint32_t type = 0;
        std::memcpy(&type, object.data() + entry, sizeof(type));
        if (type == 1) {
            const uint32_t unknownVendorType = 0x40;
            std::memcpy(object.data() + entry, &unknownVendorType, sizeof(unknownVendorType));
        }
    }

    TestBackend backend;
    ccu::runtime::setBackendForTesting(&backend);
    ccu_context_options_t options{};
    options.struct_size = sizeof(options);
    options.default_sync_timeout_ms = 30000;
    options.abort_timeout_ms = 10000;
    ccu_context_t* context = nullptr;
    ASSERT_EQ(ccu_context_create(&options, &context), CCU_STATUS_SUCCESS);
    ASSERT_EQ(::setenv("CCU_FAKE_TASK_QUERY_MODE", "success", 1), 0);
    SetFakeProfapiMode("normal");
    const uint32_t apiBefore = FakeProfapiApiReportCount();
    ASSERT_EQ(StartFakeProfiling(0x2), 0);
    const uint32_t kernelIndices[] = {1, 2};
    const ccu_image_descriptor_t image{"dav-3510", object.data(), object.size()};
    const ccu_kernel_descriptor_t descriptor{&image, 2, kernelIndices, 0};
    int stream = 0;
    ASSERT_EQ(ccu_compiler_submit(context, &descriptor, &stream, nullptr, 0, nullptr, 0), CCU_STATUS_SUCCESS);
    EXPECT_EQ(backend.launchCount, 1U);
    uint16_t level = 0;
    uint32_t type = 0;
    uint32_t threadId = 0;
    uint64_t beginTime = 0;
    uint64_t endTime = 0;
    uint64_t itemId = 0;
    ASSERT_EQ(FakeProfapiApiReport(apiBefore, &level, &type, &threadId, &beginTime, &endTime, &itemId), 0);
    EXPECT_EQ(itemId, FakeProfapiHash("ccu_kernel"));
    ASSERT_EQ(ccu_synchronize(reinterpret_cast<ccu_instance_t>(context), &stream, 0), CCU_STATUS_SUCCESS);
    ASSERT_EQ(ccu_context_destroy(&context), CCU_STATUS_SUCCESS);
    ccu::runtime::setBackendForTesting(nullptr);
}
