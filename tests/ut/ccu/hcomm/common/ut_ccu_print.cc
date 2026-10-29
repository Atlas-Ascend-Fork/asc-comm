/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu/hcomm/ccu_print.hpp"
#include "hcomm/common/ccu_print_internal.h"
#include <acl/acl_rt.h>
#include <base/dlog_pub.h>
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace ccu = AscendC::ccu;
namespace dfx = ccu::dfx;

namespace {

// Only the hardware/runtime boundary is mocked. The public header and actual
// Host consumer are compiled unchanged. CCU operations are RECORDED first and
// replayed later, so a Host wrapper handle cannot masquerade as a runtime value.
struct Instruction {
    std::function<void()> run;
    uint64_t lhs{0};
    uint64_t rhs{0};
    ccu_condition_type condition{ccu_condition_eq};
    size_t end{0};
    bool isIf{false};
    bool variableRhs{false};
};
std::vector<Instruction> program;
std::vector<size_t> conditions;
std::map<uint64_t, uint64_t> xn;
uint64_t nextHandle = 1;
bool variableComparison = false;
bool failCondition = false;
bool failStore = false;
bool failArithmeticInCondition = false;
bool failMalloc = false;
bool failGetContext = false;
thread_local aclrtContext currentContext = reinterpret_cast<void*>(1);
std::mutex runtimeMutex;
struct Allocation {
    size_t size;
    aclrtContext context;
};
std::map<void*, Allocation> allocations;
std::vector<std::string> logs;
int wrongContext = 0;

void Record(std::function<void()> run) { program.push_back({std::move(run)}); }

void Execute()
{
    std::lock_guard<std::mutex> lock(runtimeMutex);
    for (size_t pc = 0; pc < program.size(); ++pc) {
        auto& ins = program[pc];
        if (ins.isIf) {
            const bool equal = xn[ins.lhs] == (ins.variableRhs ? xn[ins.rhs] : ins.rhs);
            if ((ins.condition == ccu_condition_eq && !equal) || (ins.condition == ccu_condition_ne && equal)) {
                pc = ins.end;
            }
        } else {
            ins.run();
        }
    }
}

bool HasContext(const void* ptr, size_t bytes)
{
    const auto address = reinterpret_cast<uintptr_t>(ptr);
    for (const auto& pair : allocations) {
        const auto begin = reinterpret_cast<uintptr_t>(pair.first);
        if (address >= begin && address - begin <= pair.second.size && bytes <= pair.second.size - (address - begin)) {
            if (pair.second.context == currentContext) {
                return true;
            }
            ++wrongContext;
            return false;
        }
    }
    return false;
}

size_t RecordCount()
{
    std::lock_guard<std::mutex> lock(runtimeMutex);
    size_t count = 0;
    for (const auto& line : logs) {
        count += line.find("sample.cc:") != std::string::npos && line.find("dropped=") == std::string::npos;
    }
    return count;
}

} // namespace

// Emit the subset used by printf through the REAL ccu::variable/load/store API.
namespace asc {
CcuResult ccu_variable_alloc(ccu_variable_handle* h)
{
    *h = nextHandle++;
    return CCU_SUCCESS;
}
CcuResult ccu_variable_assign_imm(ccu_variable_handle h, uint64_t v)
{
    Record([=] { xn[h] = v; });
    return CCU_SUCCESS;
}
CcuResult ccu_variable_add_var_to_var(ccu_variable_handle out, ccu_variable_handle a, ccu_variable_handle b)
{
    if (failArithmeticInCondition && !conditions.empty()) {
        failArithmeticInCondition = false;
        return CCU_E_INTERNAL;
    }
    Record([=] { xn[out] = xn[a] + xn[b]; });
    return CCU_SUCCESS;
}
CcuResult ccu_variable_sub_var_to_var(ccu_variable_handle out, ccu_variable_handle a, ccu_variable_handle b)
{
    Record([=] { xn[out] = xn[a] - xn[b]; });
    return CCU_SUCCESS;
}
CcuResult ccu_variable_mul_var_to_var(ccu_variable_handle out, ccu_variable_handle a, ccu_variable_handle b)
{
    Record([=] { xn[out] = xn[a] * xn[b]; });
    return CCU_SUCCESS;
}
CcuResult ccu_load_var(uint64_t address, ccu_variable_handle out, uint32_t count)
{
    EXPECT_EQ(count, 1U);
    Record([=] { std::memcpy(&xn[out], reinterpret_cast<void*>(address), 8); });
    return CCU_SUCCESS;
}
CcuResult ccu_load_var_from_var_addr(ccu_variable_handle address, ccu_variable_handle out, uint32_t count)
{
    EXPECT_EQ(count, 1U);
    Record([=] { std::memcpy(&xn[out], reinterpret_cast<void*>(xn[address]), 8); });
    return CCU_SUCCESS;
}
CcuResult ccu_store_var_to_var_addr(ccu_variable_handle address, ccu_variable_handle value, uint32_t count)
{
    EXPECT_EQ(count, 1U);
    if (failStore) {
        failStore = false;
        return CCU_E_INTERNAL;
    }
    Record([=] { std::memcpy(reinterpret_cast<void*>(xn[address]), &xn[value], 8); });
    return CCU_SUCCESS;
}
CcuResult ccu_if_begin(ccu_variable_handle a, uint64_t b, ccu_condition_type condition, const char*)
{
    if (failCondition) {
        failCondition = false;
        return CCU_E_INTERNAL;
    }
    conditions.push_back(program.size());
    Instruction ins{};
    ins.lhs = a;
    ins.rhs = b;
    ins.condition = condition;
    ins.isIf = true;
    program.push_back(ins);
    return CCU_SUCCESS;
}
CcuResult ccu_if_begin_var(
    ccu_variable_handle a, ccu_variable_handle b, ccu_condition_type condition, const char* label)
{
    if (!variableComparison) {
        ADD_FAILURE() << "printf emitted an A5-unsupported variable comparison";
        return CCU_E_NOT_SUPPORT;
    }
    const auto result = ccu_if_begin(a, b, condition, label);
    if (result == CCU_SUCCESS) {
        program.back().variableRhs = true;
    }
    return result;
}
CcuResult ccu_if_end(const char*)
{
    if (conditions.empty()) {
        return CCU_E_INTERNAL;
    }
    program[conditions.back()].end = program.size();
    conditions.pop_back();
    Record([] {});
    return CCU_SUCCESS;
}
} // namespace asc

extern "C" {
aclError aclrtGetCurrentContext(aclrtContext* context)
{
    if (failGetContext) {
        return 1;
    }
    *context = currentContext;
    return ACL_SUCCESS;
}
aclError aclrtSetCurrentContext(aclrtContext context)
{
    if (context == nullptr) {
        return 1;
    }
    currentContext = context;
    return ACL_SUCCESS;
}
aclError aclrtMalloc(void** ptr, size_t bytes, aclrtMemMallocPolicy)
{
    std::lock_guard<std::mutex> lock(runtimeMutex);
    if (failMalloc) {
        return 1;
    }
    *ptr = std::calloc(1, bytes);
    if (*ptr == nullptr) {
        return 1;
    }
    allocations[*ptr] = {bytes, currentContext};
    return ACL_SUCCESS;
}
aclError aclrtFree(void* ptr)
{
    std::lock_guard<std::mutex> lock(runtimeMutex);
    auto it = allocations.find(ptr);
    if (it == allocations.end() || !HasContext(ptr, it->second.size)) {
        return 1;
    }
    allocations.erase(it);
    std::free(ptr);
    return ACL_SUCCESS;
}
aclError aclrtMemset(void* ptr, size_t capacity, int32_t value, size_t count)
{
    std::lock_guard<std::mutex> lock(runtimeMutex);
    if (count > capacity || !HasContext(ptr, count)) {
        return 1;
    }
    std::memset(ptr, value, count);
    return ACL_SUCCESS;
}
aclError aclrtMemcpy(void* dst, size_t capacity, const void* src, size_t bytes, aclrtMemcpyKind kind)
{
    std::lock_guard<std::mutex> lock(runtimeMutex);
    if (bytes > capacity || !HasContext(kind == ACL_MEMCPY_DEVICE_TO_HOST ? src : dst, bytes)) {
        return 1;
    }
    std::memcpy(dst, src, bytes);
    return ACL_SUCCESS;
}
void DlogRecord(int32_t module, int32_t, const char* format, ...)
{
    EXPECT_NE(module & RUN_LOG_MASK, 0);
    char buffer[2048];
    va_list args;
    va_start(args, format);
    (void)std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    std::lock_guard<std::mutex> lock(runtimeMutex);
    logs.emplace_back(buffer);
}
} // extern "C"

class CcuPrintTest : public testing::TestWithParam<bool> {
protected:
    dfx::CcuPrintStream* stream = dfx::CcuPrintStream::GetInstance();
    void SetUp() override
    {
        stream->Release();
        program.clear();
        conditions.clear();
        xn.clear();
        nextHandle = 1;
        variableComparison = GetParam();
        failCondition = failStore = failMalloc = failGetContext = false;
        failArithmeticInCondition = false;
        currentContext = reinterpret_cast<void*>(1);
        wrongContext = 0;
        logs.clear();
    }
    void TearDown() override
    {
        stream->Release();
        EXPECT_EQ(wrongContext, 0);
        EXPECT_TRUE(allocations.empty());
        EXPECT_TRUE(conditions.empty());
    }
    dfx::detail::PrintInfoHandle* Allocate(const char* format, size_t count)
    {
        auto* handle = stream->AllocPrintInfo("sample.cc", 42, format, count);
        stream->Stop(); // Deterministic protocol tests: consume only through Flush.
        return handle;
    }
};

TEST_P(CcuPrintTest, FormatsAndRejectsInvalidInputs)
{
    uint64_t args[] = {42, 0xabc, 0x123, 99};
    std::string output;
    ASSERT_TRUE(dfx::detail::Format("u=%u x=%x p=%p v=%v %%", args, 4, output));
    EXPECT_EQ(output, "u=42 x=abc p=0x123 v=99 %");
    for (const char* invalid : {"%", "%s", "%llu", "%d"}) {
        EXPECT_FALSE(dfx::detail::ValidateFormat(invalid, 1));
    }
    EXPECT_FALSE(dfx::detail::ValidateFormat(nullptr, 0));
    EXPECT_FALSE(dfx::detail::ValidateFormat("%u", 0));
    EXPECT_FALSE(dfx::detail::Format("%u", nullptr, 1, output));
}

TEST_P(CcuPrintTest, BuildsBeforeExecutingAndPrintsRuntimeValue)
{
    auto* handle = Allocate("value=%u", 1);
    ASSERT_NE(handle, nullptr);
    ccu::variable value;
    ASSERT_EQ(dfx::EmitPrintf(handle, value), CCU_SUCCESS);
    EXPECT_EQ(RecordCount(), 0U);
    xn[value.handle] = 12345;
    Execute();
    stream->Flush();
    ASSERT_EQ(RecordCount(), 1U);
    EXPECT_NE(logs.back().find("value=12345"), std::string::npos);
    stream->Flush();
    EXPECT_EQ(RecordCount(), 1U);
}

TEST_P(CcuPrintTest, UsesOnlyImmediateComparisons)
{
    auto* handle = Allocate("value=%u", 1);
    ASSERT_NE(handle, nullptr);
    ccu::variable value;
    ASSERT_EQ(dfx::EmitPrintf(handle, value), CCU_SUCCESS);
    size_t variableBranches = 0;
    size_t immediateBranches = 0;
    for (const auto& instruction : program) {
        if (instruction.isIf) {
            if (instruction.variableRhs) {
                ++variableBranches;
            } else {
                ++immediateBranches;
            }
        }
    }
    EXPECT_EQ(variableBranches, 0U);
    EXPECT_EQ(immediateBranches, 3U);
}

TEST_P(CcuPrintTest, FullRingDropsNewRecordsAndWrapsAfterConsumption)
{
    auto* handle = Allocate("value=%u", 1);
    ASSERT_NE(handle, nullptr);
    ccu::variable value;
    ASSERT_EQ(dfx::EmitPrintf(handle, value), CCU_SUCCESS);
    for (uint64_t i = 0; i < 80; ++i) {
        xn[value.handle] = i;
        Execute();
    }
    stream->Flush();
    EXPECT_EQ(RecordCount(), 31U);
    bool warned = false;
    for (const auto& line : logs) {
        warned |= line.find("dropped=49") != std::string::npos;
    }
    EXPECT_TRUE(warned);
    EXPECT_NE(logs.back().find("value=30"), std::string::npos);
    for (uint64_t i = 80; i < 85; ++i) {
        xn[value.handle] = i;
        Execute();
    }
    stream->Flush();
    EXPECT_EQ(RecordCount(), 36U);
    EXPECT_NE(logs.back().find("value=84"), std::string::npos);
}

TEST_P(CcuPrintTest, ZeroArgumentRecordAndPublicMacro)
{
    auto emit = []() -> CcuResult {
        CCU_PRINTF("hello %%");
        return CCU_SUCCESS;
    };
    ASSERT_EQ(emit(), CCU_SUCCESS);
    stream->Stop();
    Execute();
    stream->Flush();
    bool found = false;
    for (const auto& line : logs) {
        found |= line.find("hello %") != std::string::npos;
    }
    EXPECT_TRUE(found);
}

TEST_P(CcuPrintTest, ContextsAndRepeatedCallSitesHaveIndependentStorage)
{
    auto* first = Allocate("value=%u", 1);
    ASSERT_NE(first, nullptr);
    currentContext = reinterpret_cast<void*>(2);
    auto* second = Allocate("value=%u", 1);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first->addressCell, second->addressCell);
    ccu::variable value;
    ASSERT_EQ(dfx::EmitPrintf(first, value), CCU_SUCCESS);
    ASSERT_EQ(dfx::EmitPrintf(second, value), CCU_SUCCESS);
    xn[value.handle] = 77;
    Execute();
    stream->Flush();
    EXPECT_EQ(currentContext, reinterpret_cast<void*>(2));
    EXPECT_EQ(RecordCount(), 2U);
}

TEST_P(CcuPrintTest, RegistrationFailuresDisableOnlyDebugEmission)
{
    EXPECT_EQ(Allocate("%s", 1), nullptr);
    failMalloc = true;
    EXPECT_EQ(Allocate("%u", 1), nullptr);
    failMalloc = false;
    failGetContext = true;
    EXPECT_EQ(Allocate("%u", 1), nullptr);
    failGetContext = false;
    EXPECT_EQ(stream->AllocPrintInfo("sample.cc", 42, "", std::numeric_limits<size_t>::max()), nullptr);
    ccu::variable value;
    EXPECT_EQ(dfx::EmitPrintf(nullptr, value), CCU_SUCCESS);
    EXPECT_TRUE(program.empty());
}

TEST_P(CcuPrintTest, ConditionAndStoreBuildFailuresAreNotSilentlyIgnored)
{
    auto* handle = Allocate("%u", 1);
    ASSERT_NE(handle, nullptr);
    ccu::variable value;
    failCondition = true;
    EXPECT_EQ(dfx::EmitPrintf(handle, value), CCU_E_INTERNAL);
    failStore = true;
    EXPECT_EQ(dfx::EmitPrintf(handle, value), CCU_E_INTERNAL);
}

TEST_P(CcuPrintTest, ArithmeticBuildExceptionClosesCondition)
{
    auto* handle = Allocate("%u", 1);
    ASSERT_NE(handle, nullptr);
    ccu::variable value;
    failArithmeticInCondition = true;
    EXPECT_ANY_THROW(dfx::EmitPrintf(handle, value));
    EXPECT_FALSE(failArithmeticInCondition);
    EXPECT_TRUE(conditions.empty());
}

TEST_P(CcuPrintTest, InvalidCursorIsRejectedWithoutReadingOutsideRing)
{
    auto* handle = Allocate("value=%u", 1);
    ASSERT_NE(handle, nullptr);
    uint64_t base = 0;
    std::memcpy(&base, reinterpret_cast<void*>(handle->addressCell), 8);
    auto* header = reinterpret_cast<dfx::detail::PrintRingHeader*>(base);
    header->tail = handle->ringBytes + 8;
    stream->Flush();
    EXPECT_EQ(RecordCount(), 0U);
    EXPECT_NE(logs.back().find("invalid print ring cursor"), std::string::npos);
}

TEST_P(CcuPrintTest, PointerFormatIsCopiedAndMacroDoesNotReturnFromCaller)
{
    std::string format = "dynamic=%u";
    const char* pointer = format.c_str();
    ccu::variable value;
    bool reached = false;
    auto emit = [&]() -> CcuResult {
        const CcuResult status = CCU_PRINTF(pointer, value);
        reached = true;
        return status;
    };
    EXPECT_EQ(emit(), CCU_SUCCESS);
    EXPECT_TRUE(reached);
    stream->Stop();
    format.assign("changed");
    xn[value.handle] = 9;
    Execute();
    stream->Flush();
    EXPECT_NE(logs.back().find("dynamic=9"), std::string::npos);

    reached = false;
    pointer = "dynamic=%u";
    failCondition = true;
    EXPECT_EQ(emit(), CCU_E_INTERNAL);
    EXPECT_TRUE(reached);
}

TEST_P(CcuPrintTest, SharedSequenceOrdersDifferentCallSitesWithinScan)
{
    auto* first = Allocate("first=%u", 1);
    auto* second = Allocate("second=%u", 1);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->sequenceAddressCell, second->sequenceAddressCell);
    ccu::variable value;
    ASSERT_EQ(dfx::EmitPrintf(second, value), CCU_SUCCESS);
    ASSERT_EQ(dfx::EmitPrintf(first, value), CCU_SUCCESS);
    xn[value.handle] = 7;
    Execute();
    stream->Flush();
    ASSERT_EQ(logs.size(), 2U);
    EXPECT_NE(logs[0].find("second=7"), std::string::npos);
    EXPECT_NE(logs[1].find("first=7"), std::string::npos);
}

TEST_P(CcuPrintTest, BackgroundReaderConsumesAndCanRestart)
{
    ccu::variable value;
    for (uint64_t i = 1; i <= 2; ++i) {
        program.clear();
        auto* handle = stream->AllocPrintInfo("sample.cc", 42, "background=%u", 1);
        ASSERT_NE(handle, nullptr);
        ASSERT_EQ(dfx::EmitPrintf(handle, value), CCU_SUCCESS);
        xn[value.handle] = i;
        Execute();
        for (unsigned attempt = 0; attempt < 100 && RecordCount() < i; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        EXPECT_EQ(RecordCount(), i);
        stream->Stop();
    }
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

INSTANTIATE_TEST_SUITE_P(CcuVersions, CcuPrintTest, testing::Values(false, true));
