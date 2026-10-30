/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "detail/ccuobj_reader.h"
#include "ccu_runtime_backend.h"
#include "ccu_runtime_external.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rt = ccu::runtime;

struct ccu_external_event_set;
struct ccu_external_xn_set;
using ccu_external_event_set_t = ccu_external_event_set;
using ccu_external_xn_set_t = ccu_external_xn_set;

struct ccu_external_arg_t {
    uint32_t kind;
    uint32_t count;
    void* set;
    uint64_t reserved[2];
};

namespace {

constexpr uint32_t kCcuobjVersion = 1;
constexpr uint32_t kCcuobjAbiRevision = 4;
constexpr uint32_t kMaxSpecializationBitWidth = 64;
constexpr const char* kTarget = "a5";

template <size_t N>
bool allZero(const uint64_t (&values)[N])
{
    for (uint64_t value : values)
        if (value != 0)
            return false;
    return true;
}

bool validStatus(ccu_status_t status) { return status >= CCU_STATUS_SUCCESS && status <= CCU_STATUS_CONTEXT_POISONED; }

ccu_status_t normalizeStatus(ccu_status_t status) { return validStatus(status) ? status : CCU_STATUS_RUNTIME_ERROR; }

bool validAbortAction(ccu_abort_action_t action) { return action >= CCU_ABORT_NONE && action <= CCU_ABORT_DEVICE; }

ccu_status_t invalidObject(const char* reason)
{
    const char* diagnostics = std::getenv("CCU_RUNTIME_DIAGNOSTICS");
    if (diagnostics && *diagnostics && std::strcmp(diagnostics, "0") != 0)
        std::fprintf(stderr, "CCUC-RUNTIME: invalid object: %s\n", reason);
    return CCU_STATUS_INVALID_OBJECT;
}

constexpr uint32_t kBitsPerByte = 8;
constexpr size_t kCcuobjHeaderSize = 64;
constexpr uint32_t kSha256WordBits = 32;
constexpr size_t kSha256BlockSize = 64;
constexpr size_t kSha256WordsPerBlock = 16;
constexpr size_t kSha256WordCount = 64;
constexpr size_t kSha256StateSize = 8;
constexpr size_t kSha256DigestBytes = 32;
constexpr size_t kSha256TailSize = 128;
constexpr size_t kSha256LengthBytes = 8;
constexpr size_t kSha256BytesPerWord = 4;
constexpr uint32_t kSha256FirstByteShift = 24;
constexpr uint32_t kSha256SecondByteShift = 16;
constexpr uint32_t kSha256ThirdByteShift = 8;
constexpr size_t kSha256TailDataLimit = 56;
constexpr size_t kSha256StateByte0Shift = 24;
constexpr size_t kSha256StateByte1Shift = 16;
constexpr size_t kSha256StateByte2Shift = 8;

uint32_t rotateRight(uint32_t value, uint32_t amount)
{
    return (value >> amount) | (value << (kSha256WordBits - amount));
}

constexpr uint32_t kSha256FirstWordOffset = 15;
constexpr uint32_t kSha256SecondWordOffset = 2;
constexpr uint32_t kSha256FirstRotate7 = 7;
constexpr uint32_t kSha256FirstRotate18 = 18;
constexpr uint32_t kSha256FirstShift3 = 3;
constexpr uint32_t kSha256SecondRotate17 = 17;
constexpr uint32_t kSha256SecondRotate19 = 19;
constexpr uint32_t kSha256SecondShift10 = 10;

void loadSha256Words(uint32_t words[kSha256WordCount], const uint8_t block[kSha256BlockSize])
{
    for (size_t index = 0; index < kSha256WordsPerBlock; ++index) {
        const size_t offset = index * kSha256BytesPerWord;
        words[index] = (static_cast<uint32_t>(block[offset]) << kSha256FirstByteShift) |
                       (static_cast<uint32_t>(block[offset + 1]) << kSha256SecondByteShift) |
                       (static_cast<uint32_t>(block[offset + 2]) << kSha256ThirdByteShift) |
                       static_cast<uint32_t>(block[offset + 3]);
    }
    for (size_t index = kSha256WordsPerBlock; index < kSha256WordCount; ++index) {
        const uint32_t prior15 = words[index - kSha256FirstWordOffset];
        const uint32_t prior2 = words[index - kSha256SecondWordOffset];
        const uint32_t s0 = rotateRight(prior15, kSha256FirstRotate7) ^ rotateRight(prior15, kSha256FirstRotate18) ^
                            (prior15 >> kSha256FirstShift3);
        const uint32_t s1 = rotateRight(prior2, kSha256SecondRotate17) ^ rotateRight(prior2, kSha256SecondRotate19) ^
                            (prior2 >> kSha256SecondShift10);
        words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }
}

struct Sha256Work {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
};

Sha256Work initialSha256Work(const std::array<uint32_t, 8>& state)
{
    return {state[0], state[1], state[2], state[3], state[4], state[5], state[6], state[7]};
}

void compressSha256Word(Sha256Work& work, uint32_t constant, uint32_t word)
{
    const uint32_t s1 = rotateRight(work.e, 6) ^ rotateRight(work.e, 11) ^ rotateRight(work.e, 25);
    const uint32_t choose = (work.e & work.f) ^ (~work.e & work.g);
    const uint32_t first = work.h + s1 + choose + constant + word;
    const uint32_t s0 = rotateRight(work.a, 2) ^ rotateRight(work.a, 13) ^ rotateRight(work.a, 22);
    const uint32_t majority = (work.a & work.b) ^ (work.a & work.c) ^ (work.b & work.c);
    const uint32_t second = s0 + majority;
    work.h = work.g;
    work.g = work.f;
    work.f = work.e;
    work.e = work.d + first;
    work.d = work.c;
    work.c = work.b;
    work.b = work.a;
    work.a = first + second;
}

void sha256Transform(std::array<uint32_t, 8>& state, const uint8_t block[kSha256BlockSize])
{
    static constexpr uint32_t constants[kSha256WordCount] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

    uint32_t words[kSha256WordCount];
    loadSha256Words(words, block);
    Sha256Work work = initialSha256Work(state);
    for (size_t index = 0; index < kSha256WordCount; ++index)
        compressSha256Word(work, constants[index], words[index]);

    state[0] += work.a;
    state[1] += work.b;
    state[2] += work.c;
    state[3] += work.d;
    state[4] += work.e;
    state[5] += work.f;
    state[6] += work.g;
    state[7] += work.h;
}

std::array<uint8_t, kSha256DigestBytes> sha256(const uint8_t* data, size_t size)
{
    std::array<uint32_t, kSha256StateSize> state = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    size_t offset = 0;
    while (size - offset >= kSha256BlockSize) {
        sha256Transform(state, data + offset);
        offset += kSha256BlockSize;
    }

    uint8_t tail[kSha256TailSize] = {};
    const size_t remainder = size - offset;
    auto* dst = std::begin(tail);
    std::copy(data + offset, data + offset + remainder, dst);
    tail[remainder] = 0x80;
    const size_t tailSize = remainder < kSha256TailDataLimit ? kSha256BlockSize : kSha256TailSize;
    const uint64_t bitLength = static_cast<uint64_t>(size) * kBitsPerByte;
    for (size_t index = 0; index < kSha256LengthBytes; ++index)
        tail[tailSize - 1 - index] = static_cast<uint8_t>(bitLength >> (index * kBitsPerByte));
    sha256Transform(state, tail);
    if (tailSize == kSha256TailSize)
        sha256Transform(state, tail + kSha256BlockSize);

    std::array<uint8_t, kSha256DigestBytes> digest{};
    for (size_t index = 0; index < state.size(); ++index) {
        digest[index * kSha256BytesPerWord] = static_cast<uint8_t>(state[index] >> kSha256StateByte0Shift);
        digest[index * kSha256BytesPerWord + 1] = static_cast<uint8_t>(state[index] >> kSha256StateByte1Shift);
        digest[index * kSha256BytesPerWord + 2] = static_cast<uint8_t>(state[index] >> kSha256StateByte2Shift);
        digest[index * kSha256BytesPerWord + 3] = static_cast<uint8_t>(state[index]);
    }
    return digest;
}

uint64_t payloadIdentity(const std::array<uint8_t, kSha256DigestBytes>& digest)
{
    uint64_t identity = 0;
    for (size_t index = 0; index < kSha256LengthBytes; ++index)
        identity |= static_cast<uint64_t>(digest[index]) << (index * kBitsPerByte);
    return identity;
}

struct OwnedDescriptor {
    ccu_image_descriptor_t image{};
    ccu_kernel_descriptor_t descriptor{};
    ccu_rank_entry_t selectedRank{};
    std::string family;
    std::string specialization;
    std::string target;
    std::vector<uint8_t> blob;
    std::array<uint8_t, kSha256DigestBytes> digest{};
    std::vector<ccu_specialization_value_t> values;
    std::vector<std::string> valueTypeNames;
    std::vector<ccu_rank_entry_t> ranks;
    std::vector<ccu_task_arg_schema_t> taskArgs;
    std::vector<std::string> taskArgNames;

    void finalizePointers()
    {
        image.target = target.c_str();
        image.ccuobj = blob.data();
        image.sha256 = digest.data();
        descriptor.family_identity = family.c_str();
        descriptor.specialization_identity = specialization.c_str();
        descriptor.image = &image;
        descriptor.specialization_values = values.empty() ? nullptr : values.data();
        descriptor.rank_entries = ranks.data();
        descriptor.task_args = taskArgs.empty() ? nullptr : taskArgs.data();
        for (size_t index = 0; index < values.size(); ++index)
            values[index].type_identity = valueTypeNames[index].empty() ? nullptr : valueTypeNames[index].c_str();
        for (size_t index = 0; index < taskArgs.size(); ++index)
            taskArgs[index].name = taskArgNames[index].c_str();
    }
};

struct ValidatedDescriptor {
    ccu::ccuobj::ParsedModule parsed;
    std::array<uint8_t, kSha256DigestBytes> digest{};
    uint64_t identity = 0;
    const ccu_rank_entry_t* selected = nullptr;
};

ccu_status_t validateDescriptorHeader(const ccu_kernel_descriptor_t* source)
{
    if (!source || source->struct_size != sizeof(*source) || !source->family_identity || !*source->family_identity ||
        !source->specialization_identity || !*source->specialization_identity || !source->image ||
        !allZero(source->reserved))
        return invalidObject("descriptor-header");
    if (source->stub_abi_version != CCU_STUB_ABI_VERSION)
        return CCU_STATUS_VERSION_MISMATCH;
    return CCU_STATUS_SUCCESS;
}

ccu_status_t validateImageHeader(const ccu_image_descriptor_t& image)
{
    if (image.struct_size != sizeof(image) || image.flags != 0 || !image.target ||
        std::strcmp(image.target, kTarget) != 0 || !image.ccuobj || image.ccuobj_size < kCcuobjHeaderSize ||
        !image.sha256 || image.ccuobj_size > static_cast<uint64_t>(SIZE_MAX) ||
        image.ccuobj_size > UINT64_MAX / kBitsPerByte)
        return invalidObject("image-header");
    if (image.object_abi_version != CCU_OBJECT_ABI_VERSION || image.ccuobj_abi_revision != kCcuobjAbiRevision)
        return CCU_STATUS_VERSION_MISMATCH;
    return CCU_STATUS_SUCCESS;
}

ccu_status_t validatePayload(const ccu_image_descriptor_t& image, ValidatedDescriptor& validated)
{
    if (!ccu::ccuobj::parseCcuobj(image.ccuobj, static_cast<size_t>(image.ccuobj_size), validated.parsed))
        return invalidObject("ccuobj-parse");
    if (validated.parsed.version != kCcuobjVersion || validated.parsed.abiRev != kCcuobjAbiRevision)
        return CCU_STATUS_VERSION_MISMATCH;

    validated.digest = sha256(image.ccuobj, static_cast<size_t>(image.ccuobj_size));
    if (std::memcmp(validated.digest.data(), image.sha256, validated.digest.size()) != 0)
        return invalidObject("ccuobj-sha256");
    validated.identity = payloadIdentity(validated.digest);
    return CCU_STATUS_SUCCESS;
}

ccu_status_t validateDescriptorSchema(
    const ccu_kernel_descriptor_t* source, uint32_t contextRank, uint32_t contextRankSize)
{
    if (source->communicator_size == 0 || source->communicator_size != contextRankSize ||
        contextRank >= contextRankSize || source->rank_entry_count != contextRankSize || !source->rank_entries ||
        source->implicit_peer_count != contextRankSize - 1)
        return CCU_STATUS_SCHEMA_MISMATCH;
    if ((source->task_arg_count != 0 && !source->task_args) ||
        (source->specialization_value_count != 0 && !source->specialization_values))
        return invalidObject("schema-pointers");
    return CCU_STATUS_SUCCESS;
}

ccu_status_t validateRanks(
    const ccu_kernel_descriptor_t* source, uint32_t contextRank, uint32_t contextRankSize,
    ValidatedDescriptor& validated)
{
    std::vector<bool> seenRanks(contextRankSize, false);
    for (uint32_t index = 0; index < source->rank_entry_count; ++index) {
        const ccu_rank_entry_t& rank = source->rank_entries[index];
        if (rank.communicator_rank >= contextRankSize || seenRanks[rank.communicator_rank] ||
            rank.kernel_index >= validated.parsed.kernels.size() || rank.payload_identity != validated.identity)
            return CCU_STATUS_SCHEMA_MISMATCH;

        const ccu::ccuobj::ParsedKernel& kernel = validated.parsed.kernels[rank.kernel_index];
        if (!kernel.microcode || kernel.instrCount == 0 || kernel.taskArgCount != source->task_arg_count ||
            kernel.channelArgCount > source->implicit_peer_count)
            return CCU_STATUS_SCHEMA_MISMATCH;

        seenRanks[rank.communicator_rank] = true;
        if (rank.communicator_rank == contextRank)
            validated.selected = &rank;
    }
    if (!validated.selected)
        return CCU_STATUS_SCHEMA_MISMATCH;
    return CCU_STATUS_SUCCESS;
}

ccu_status_t validateTaskArgs(const ccu_kernel_descriptor_t* source)
{
    for (uint32_t index = 0; index < source->task_arg_count; ++index) {
        const ccu_task_arg_schema_t& argument = source->task_args[index];
        if ((argument.kind != CCU_TASK_ARG_U64 && argument.kind != CCU_TASK_ARG_I64) || argument.flags != 0 ||
            !argument.name || !*argument.name)
            return CCU_STATUS_SCHEMA_MISMATCH;
    }
    return CCU_STATUS_SUCCESS;
}

ccu_status_t validateSpecializationValues(const ccu_kernel_descriptor_t* source)
{
    for (uint32_t index = 0; index < source->specialization_value_count; ++index) {
        const ccu_specialization_value_t& value = source->specialization_values[index];
        if ((value.kind != CCU_SPECIALIZATION_UNSIGNED && value.kind != CCU_SPECIALIZATION_SIGNED &&
             value.kind != CCU_SPECIALIZATION_ENUM) ||
            value.bit_width == 0 || value.bit_width > kMaxSpecializationBitWidth ||
            (value.kind == CCU_SPECIALIZATION_ENUM && (!value.type_identity || !*value.type_identity)))
            return CCU_STATUS_SCHEMA_MISMATCH;
    }
    return CCU_STATUS_SUCCESS;
}

void copyOwnedStrings(
    OwnedDescriptor& result, const ccu_kernel_descriptor_t* source, const ccu_image_descriptor_t& image)
{
    result.family = source->family_identity;
    result.specialization = source->specialization_identity;
    result.target = image.target;
    result.valueTypeNames.reserve(result.values.size());
    for (const ccu_specialization_value_t& value : result.values)
        result.valueTypeNames.emplace_back(value.type_identity ? value.type_identity : "");
    result.taskArgNames.reserve(result.taskArgs.size());
    for (const ccu_task_arg_schema_t& argument : result.taskArgs)
        result.taskArgNames.emplace_back(argument.name);
}

void copyOwnedArrays(
    OwnedDescriptor& result, const ccu_kernel_descriptor_t* source, const ccu_image_descriptor_t& image,
    const ccu_rank_entry_t* selected)
{
    result.blob.assign(image.ccuobj, image.ccuobj + static_cast<size_t>(image.ccuobj_size));
    result.image = image;
    result.image.ccuobj_size = result.blob.size();
    result.descriptor = *source;

    if (source->specialization_value_count != 0)
        result.values.assign(
            source->specialization_values, source->specialization_values + source->specialization_value_count);
    result.ranks.assign(source->rank_entries, source->rank_entries + source->rank_entry_count);
    result.selectedRank = *selected;
    if (source->task_arg_count != 0)
        result.taskArgs.assign(source->task_args, source->task_args + source->task_arg_count);
}

ccu_status_t makeOwnedDescriptor(
    const ccu_kernel_descriptor_t* source, const ccu_image_descriptor_t& image, const ValidatedDescriptor& validated,
    std::unique_ptr<OwnedDescriptor>& owned)
{
    try {
        auto result = std::make_unique<OwnedDescriptor>();
        copyOwnedArrays(*result, source, image, validated.selected);
        copyOwnedStrings(*result, source, image);
        result->digest = validated.digest;
        result->finalizePointers();
        owned = std::move(result);
    } catch (const std::bad_alloc&) {
        return CCU_STATUS_RESOURCE_EXHAUSTED;
    }
    return CCU_STATUS_SUCCESS;
}

ccu_status_t copyAndValidateDescriptorImpl(
    const ccu_kernel_descriptor_t* source, uint32_t contextRank, uint32_t contextRankSize,
    std::unique_ptr<OwnedDescriptor>& owned)
{
    ccu_status_t status = validateDescriptorHeader(source);
    if (status != CCU_STATUS_SUCCESS)
        return status;

    const ccu_image_descriptor_t& image = *source->image;
    status = validateImageHeader(image);
    if (status != CCU_STATUS_SUCCESS)
        return status;

    ValidatedDescriptor validated;
    status = validatePayload(image, validated);
    if (status != CCU_STATUS_SUCCESS)
        return status;

    status = validateDescriptorSchema(source, contextRank, contextRankSize);
    if (status != CCU_STATUS_SUCCESS)
        return status;
    status = validateRanks(source, contextRank, contextRankSize, validated);
    if (status != CCU_STATUS_SUCCESS)
        return status;
    status = validateTaskArgs(source);
    if (status != CCU_STATUS_SUCCESS)
        return status;
    status = validateSpecializationValues(source);
    if (status != CCU_STATUS_SUCCESS)
        return status;

    return makeOwnedDescriptor(source, image, validated, owned);
}

ccu_status_t copyAndValidateDescriptor(
    const ccu_kernel_descriptor_t* source, uint32_t contextRank, uint32_t contextRankSize,
    std::unique_ptr<OwnedDescriptor>& owned)
{
    try {
        return copyAndValidateDescriptorImpl(source, contextRank, contextRankSize, owned);
    } catch (const std::bad_alloc&) {
        return CCU_STATUS_RESOURCE_EXHAUSTED;
    } catch (...) {
        return CCU_STATUS_RUNTIME_ERROR;
    }
}

rt::BackendSyncResult normalizeSyncResult(rt::BackendSyncResult result)
{
    if (!validStatus(result.status) || !validAbortAction(result.abortAction))
        return {CCU_STATUS_RECOVERY_FAILED, CCU_ABORT_NONE, false};
    if (result.status == CCU_STATUS_TIMEOUT_RECOVERED && (result.abortAction != CCU_ABORT_DEVICE || !result.terminal))
        return {CCU_STATUS_RECOVERY_FAILED, result.abortAction, false};
    if (result.status == CCU_STATUS_SUCCESS && !result.terminal)
        return {CCU_STATUS_RECOVERY_FAILED, result.abortAction, false};
    if (result.status == CCU_STATUS_RECOVERY_FAILED)
        result.terminal = false;
    return result;
}

} // namespace

struct ccu_kernel;
struct ccu_launch;
struct ccu_external_event_set;
struct ccu_external_xn_set;

struct ccu_context {
    std::mutex mutex;
    rt::Backend* backend = nullptr;
    void* backendContext = nullptr;
    uint32_t rank = 0;
    uint32_t rankSize = 0;
    uint64_t defaultSyncTimeoutMs = 0;
    uint64_t abortTimeoutMs = 0;
    ccu_instance_t boundInstance = 0;
    bool poisoned = false;
    ccu_launch* pendingLaunch = nullptr;
    std::unordered_set<ccu_external_event_set*> externalEventSets;
    std::unordered_set<ccu_external_xn_set*> externalXnSets;
};

/* The generated launcher carries the public instance value, while the
 * compiler runtime still owns the context object.  Keep that association in
 * the facade; the backend receives the communicator-owned HComm instance
 * unchanged. */
std::mutex instanceRegistryMutex;
std::unordered_map<ccu_instance_t, ccu_context*> instanceRegistry;

struct ccu_kernel {
    ccu_context* context = nullptr;
    std::unique_ptr<OwnedDescriptor> owned;
};

struct LaunchResult {
    ccu_status_t status = CCU_STATUS_RUNTIME_ERROR;
    ccu_abort_action_t abortAction = CCU_ABORT_NONE;
};

struct ccu_launch {
    ccu_context* context = nullptr;
    ccu_kernel* kernel = nullptr;
    void* backendLaunch = nullptr;
    void* stream = nullptr;
    bool terminal = false;
    bool hasResult = false;
    LaunchResult result{};
};

struct ccu_external_event_set {
    ccu_context* context = nullptr;
    void* backendSet = nullptr;
    uint32_t count = 0;
};

struct ccu_external_xn_set {
    ccu_context* context = nullptr;
    void* backendSet = nullptr;
    uint32_t count = 0;
};

namespace {

ccu_status_t buildBackendExternalArgs(
    ccu_context* context, const ccu_external_arg_t* externalArguments, uint32_t externalArgumentCount,
    std::vector<rt::BackendExternalArg>& backendExternalArguments)
{
    if (externalArgumentCount != 0 && !externalArguments)
        return CCU_STATUS_INVALID_ARGUMENT;
    backendExternalArguments.clear();
    backendExternalArguments.reserve(externalArgumentCount);
    for (uint32_t i = 0; i < externalArgumentCount; ++i) {
        const ccu_external_arg_t& argument = externalArguments[i];
        if (argument.count == 0 || !argument.set || !allZero(argument.reserved))
            return CCU_STATUS_INVALID_ARGUMENT;
        rt::BackendExternalArg backendArgument{};
        backendArgument.kind = argument.kind;
        backendArgument.count = argument.count;
        if (argument.kind == CCU_EXTERNAL_ARG_EVENT) {
            auto* set = static_cast<ccu_external_event_set*>(argument.set);
            if (!set || set->context != context || set->count != argument.count ||
                context->externalEventSets.find(set) == context->externalEventSets.end())
                return CCU_STATUS_INVALID_ARGUMENT;
            backendArgument.set = set->backendSet;
        } else if (argument.kind == CCU_EXTERNAL_ARG_XN) {
            auto* set = static_cast<ccu_external_xn_set*>(argument.set);
            if (!set || set->context != context || set->count != argument.count ||
                context->externalXnSets.find(set) == context->externalXnSets.end())
                return CCU_STATUS_INVALID_ARGUMENT;
            backendArgument.set = set->backendSet;
        } else {
            return CCU_STATUS_INVALID_ARGUMENT;
        }
        if (!backendArgument.set)
            return CCU_STATUS_RUNTIME_ERROR;
        backendExternalArguments.push_back(backendArgument);
    }
    return CCU_STATUS_SUCCESS;
}

ccu_status_t synchronizeLocked(ccu_launch* launch, uint64_t timeoutMs)
{
    if (launch->terminal)
        return launch->hasResult ? launch->result.status : CCU_STATUS_SUCCESS;
    const uint64_t effectiveTimeout = timeoutMs == 0 ? launch->context->defaultSyncTimeoutMs : timeoutMs;
    rt::BackendSyncResult backendResult = normalizeSyncResult(launch->context->backend->synchronize(
        launch->backendLaunch, effectiveTimeout, launch->context->abortTimeoutMs));
    launch->result = {backendResult.status, backendResult.abortAction};
    launch->hasResult = true;
    launch->terminal = backendResult.terminal;
    // HComm's device-abort callback suspends and cleans its communicator. Once
    // device-wide recovery was entered, or recovery could not prove a terminal
    // state, this context is teardown-only and must never submit another mission.
    if (backendResult.abortAction == CCU_ABORT_DEVICE || backendResult.status == CCU_STATUS_RECOVERY_FAILED)
        launch->context->poisoned = true;
    return backendResult.status;
}

struct CleanupResult {
    ccu_status_t status;
    bool released;
};

CleanupResult cleanupLaunchLocked(ccu_launch* launch)
{
    ccu_context* context = launch->context;
    ccu_status_t status = CCU_STATUS_SUCCESS;
    if (!launch->terminal) {
        status = synchronizeLocked(launch, 0);
        if (!launch->terminal)
            return {status, false};
    } else if (launch->hasResult) {
        status = launch->result.status;
    }
    const ccu_status_t releaseStatus = normalizeStatus(context->backend->launchDestroy(launch->backendLaunch));
    if (releaseStatus != CCU_STATUS_SUCCESS)
        return {releaseStatus, false};
    if (context->pendingLaunch == launch)
        context->pendingLaunch = nullptr;
    delete launch->kernel;
    delete launch;
    return {status, true};
}

} // namespace

#pragma GCC visibility push(default)
extern "C" {
ccu_status_t ccu_context_create(const ccu_context_options_t* options, ccu_context_t** context)
{
    if (!context || !options || options->struct_size != sizeof(*options) ||
        options->api_version != CCU_RUNTIME_API_VERSION || options->default_sync_timeout_ms == 0 ||
        options->abort_timeout_ms == 0 || !allZero(options->reserved))
        return CCU_STATUS_INVALID_ARGUMENT;
    try {
        auto result = std::make_unique<ccu_context>();
        result->backend = &rt::systemBackend();
        result->defaultSyncTimeoutMs = options->default_sync_timeout_ms;
        result->abortTimeoutMs = options->abort_timeout_ms;
        const ccu_status_t status = normalizeStatus(result->backend->contextCreate(
            options->communicator, &result->backendContext, &result->rank, &result->rankSize));
        if (status != CCU_STATUS_SUCCESS) {
            if (result->backendContext &&
                normalizeStatus(result->backend->contextDestroy(result->backendContext)) != CCU_STATUS_SUCCESS)
                return CCU_STATUS_RECOVERY_FAILED;
            return status;
        }
        if (!result->backendContext || result->rankSize == 0 || result->rank >= result->rankSize) {
            if (result->backendContext &&
                normalizeStatus(result->backend->contextDestroy(result->backendContext)) != CCU_STATUS_SUCCESS)
                return CCU_STATUS_RECOVERY_FAILED;
            return CCU_STATUS_RUNTIME_ERROR;
        }
        ccu_context* created = result.release();
        {
            std::lock_guard<std::mutex> instanceLock(instanceRegistryMutex);
            instanceRegistry[reinterpret_cast<ccu_instance_t>(created)] = created;
        }
        *context = created;
        return CCU_STATUS_SUCCESS;
    } catch (const std::bad_alloc&) {
        return CCU_STATUS_RESOURCE_EXHAUSTED;
    } catch (...) {
        return CCU_STATUS_RUNTIME_ERROR;
    }
}

ccu_status_t cleanupPendingLaunchesLocked(ccu_context* context)
{
    ccu_status_t overall = CCU_STATUS_SUCCESS;
    while (context->pendingLaunch) {
        CleanupResult cleanup = cleanupLaunchLocked(context->pendingLaunch);
        if (!cleanup.released)
            return cleanup.status;
        if (overall == CCU_STATUS_SUCCESS && cleanup.status != CCU_STATUS_SUCCESS)
            overall = cleanup.status;
    }
    return overall;
}

void deleteExternalEventSets(ccu_context* context)
{
    while (!context->externalEventSets.empty()) {
        ccu_external_event_set* set = *context->externalEventSets.begin();
        context->externalEventSets.erase(set);
        delete set;
    }
}

void deleteExternalXnSets(ccu_context* context)
{
    while (!context->externalXnSets.empty()) {
        ccu_external_xn_set* set = *context->externalXnSets.begin();
        context->externalXnSets.erase(set);
        delete set;
    }
}

void unregisterContextInstance(ccu_context* context)
{
    std::lock_guard<std::mutex> instanceLock(instanceRegistryMutex);
    if (context->boundInstance != 0) {
        auto bound = instanceRegistry.find(context->boundInstance);
        if (bound != instanceRegistry.end() && bound->second == context)
            instanceRegistry.erase(bound);
    }
    auto address = instanceRegistry.find(reinterpret_cast<ccu_instance_t>(context));
    if (address != instanceRegistry.end() && address->second == context)
        instanceRegistry.erase(address);
}

ccu_status_t destroyContextLocked(ccu_context* context, std::unique_lock<std::mutex>& lock, ccu_status_t cleanupStatus)
{
    deleteExternalEventSets(context);
    deleteExternalXnSets(context);

    const ccu_status_t destroyStatus = normalizeStatus(context->backend->contextDestroy(context->backendContext));
    if (destroyStatus != CCU_STATUS_SUCCESS)
        return destroyStatus;

    unregisterContextInstance(context);
    lock.unlock();
    delete context;
    return cleanupStatus;
}

ccu_status_t ccu_context_destroy(ccu_context_t** contextPointer)
{
    try {
        if (!contextPointer)
            return CCU_STATUS_INVALID_ARGUMENT;
        ccu_context* context = *contextPointer;
        if (!context)
            return CCU_STATUS_SUCCESS;

        std::unique_lock<std::mutex> lock(context->mutex);
        const ccu_status_t cleanupStatus = cleanupPendingLaunchesLocked(context);
        if (context->pendingLaunch)
            return cleanupStatus;
        *contextPointer = nullptr;
        return destroyContextLocked(context, lock, cleanupStatus);
    } catch (...) {
        return CCU_STATUS_RUNTIME_ERROR;
    }
}

ccu_status_t ccu_context_external_event_va(
    ccu_context_t* context, uint32_t count, ccu_external_event_set_t** eventSet, uint64_t* vas)
{
    try {
        if (!context || count == 0 || !eventSet || *eventSet || !vas)
            return CCU_STATUS_INVALID_ARGUMENT;
        auto created = std::make_unique<ccu_external_event_set>();
        created->context = context;
        created->count = count;
        std::vector<uint64_t> allocatedVas(count);
        std::lock_guard<std::mutex> lock(context->mutex);
        if (context->poisoned)
            return CCU_STATUS_CONTEXT_POISONED;
        void* backendSet = nullptr;
        const ccu_status_t status = normalizeStatus(
            context->backend->externalEventVa(context->backendContext, count, &backendSet, allocatedVas.data()));
        if (status != CCU_STATUS_SUCCESS || !backendSet)
            return status == CCU_STATUS_SUCCESS ? CCU_STATUS_RUNTIME_ERROR : status;
        for (uint32_t i = 0; i < count; ++i)
            if (allocatedVas[i] == 0)
                return CCU_STATUS_RUNTIME_ERROR;
        created->backendSet = backendSet;
        std::copy(allocatedVas.begin(), allocatedVas.end(), vas);
        context->externalEventSets.insert(created.get());
        *eventSet = created.release();
        return CCU_STATUS_SUCCESS;
    } catch (const std::bad_alloc&) {
        return CCU_STATUS_RESOURCE_EXHAUSTED;
    } catch (...) {
        return CCU_STATUS_RUNTIME_ERROR;
    }
}

ccu_status_t ccu_context_external_xn_va(
    ccu_context_t* context, uint32_t count, ccu_external_xn_set_t** xnSet, uint64_t* vas)
{
    try {
        if (!context || count == 0 || !xnSet || *xnSet || !vas)
            return CCU_STATUS_INVALID_ARGUMENT;
        auto created = std::make_unique<ccu_external_xn_set>();
        created->context = context;
        created->count = count;
        std::vector<uint64_t> allocatedVas(count);
        std::lock_guard<std::mutex> lock(context->mutex);
        if (context->poisoned)
            return CCU_STATUS_CONTEXT_POISONED;
        void* backendSet = nullptr;
        const ccu_status_t status = normalizeStatus(
            context->backend->externalXnVa(context->backendContext, count, &backendSet, allocatedVas.data()));
        if (status != CCU_STATUS_SUCCESS || !backendSet)
            return status == CCU_STATUS_SUCCESS ? CCU_STATUS_RUNTIME_ERROR : status;
        for (uint32_t i = 0; i < count; ++i)
            if (allocatedVas[i] == 0)
                return CCU_STATUS_RUNTIME_ERROR;
        created->backendSet = backendSet;
        std::copy(allocatedVas.begin(), allocatedVas.end(), vas);
        context->externalXnSets.insert(created.get());
        *xnSet = created.release();
        return CCU_STATUS_SUCCESS;
    } catch (const std::bad_alloc&) {
        return CCU_STATUS_RESOURCE_EXHAUSTED;
    } catch (...) {
        return CCU_STATUS_RUNTIME_ERROR;
    }
}

ccu_status_t ccu_compiler_submit_with_schedule(
    ccu_context_t* context, const ccu_kernel_descriptor_t* descriptor, HcomCcuSchd schd, void* stream,
    const ccu_task_arg_t* arguments, uint32_t argumentCount, const ccu_external_arg_t* externalArguments,
    uint32_t externalArgumentCount)
{
    if (!context || !descriptor || !stream || (argumentCount != 0 && !arguments) ||
        (externalArgumentCount != 0 && !externalArguments))
        return CCU_STATUS_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(context->mutex);
        if (context->poisoned)
            return CCU_STATUS_CONTEXT_POISONED;
        if (context->pendingLaunch)
            return CCU_STATUS_BUSY;

        std::unique_ptr<OwnedDescriptor> owned;
        ccu_status_t status = copyAndValidateDescriptor(descriptor, context->rank, context->rankSize, owned);
        if (status != CCU_STATUS_SUCCESS)
            return status;
        if (argumentCount != owned->descriptor.task_arg_count)
            return CCU_STATUS_SCHEMA_MISMATCH;
        std::vector<rt::BackendExternalArg> backendExternalArguments;
        status = buildBackendExternalArgs(context, externalArguments, externalArgumentCount, backendExternalArguments);
        if (status != CCU_STATUS_SUCCESS)
            return status;

        auto kernel = std::make_unique<ccu_kernel>();
        kernel->context = context;
        kernel->owned = std::move(owned);
        auto launch = std::make_unique<ccu_launch>();
        launch->context = context;
        launch->kernel = kernel.get();
        launch->stream = stream;
        status = normalizeStatus(context->backend->launch(
            context->backendContext, &kernel->owned->descriptor, &kernel->owned->selectedRank, stream, schd, arguments,
            argumentCount, backendExternalArguments.empty() ? nullptr : backendExternalArguments.data(),
            static_cast<uint32_t>(backendExternalArguments.size()), &launch->backendLaunch));
        if (!launch->backendLaunch) {
            return status == CCU_STATUS_SUCCESS ? CCU_STATUS_RUNTIME_ERROR : status;
        }

        launch->kernel = kernel.release();
        context->pendingLaunch = launch.release();
        return status;
    } catch (const std::bad_alloc&) {
        return CCU_STATUS_RESOURCE_EXHAUSTED;
    } catch (...) {
        return CCU_STATUS_RUNTIME_ERROR;
    }
}

ccu_status_t ccu_compiler_submit(
    ccu_context_t* context, const ccu_kernel_descriptor_t* descriptor, void* stream, const ccu_task_arg_t* arguments,
    uint32_t argumentCount, const ccu_external_arg_t* externalArguments, uint32_t externalArgumentCount)
{
    return ccu_compiler_submit_with_schedule(
        context, descriptor, HcomCcuSchd{}, stream, arguments, argumentCount, externalArguments, externalArgumentCount);
}

ccu_status_t ccu_context_bind_instance(ccu_context_t* context, ccu_instance_t* instance)
{
    if (!context || !instance || *instance == 0)
        return CCU_STATUS_INVALID_ARGUMENT;
    ccu_instance_t requested = *instance;
    std::lock_guard<std::mutex> contextLock(context->mutex);
    std::lock_guard<std::mutex> instanceLock(instanceRegistryMutex);
    auto it = instanceRegistry.find(requested);
    if (it != instanceRegistry.end() && it->second != context)
        return CCU_STATUS_BUSY;
    if (context->boundInstance != 0 && context->boundInstance != requested)
        return CCU_STATUS_BUSY;
    const ccu_status_t bindStatus = context->backend->bindInstance(context->backendContext, requested);
    if (bindStatus != CCU_STATUS_SUCCESS)
        return bindStatus;
    instanceRegistry[requested] = context;
    context->boundInstance = requested;
    *instance = requested;
    return CCU_STATUS_SUCCESS;
}

ccu_status_t ccu_compiler_submit_by_instance(
    ccu_instance_t instance, const ccu_kernel_descriptor_t* descriptor, HcomCcuSchd schd, void* stream,
    const ccu_task_arg_t* arguments, uint32_t argumentCount, const ccu_external_arg_t* externalArguments,
    uint32_t externalArgumentCount)
{
    if (instance == 0)
        return CCU_STATUS_INVALID_ARGUMENT;
    ccu_context_t* context = nullptr;
    {
        std::lock_guard<std::mutex> instanceLock(instanceRegistryMutex);
        auto it = instanceRegistry.find(instance);
        if (it == instanceRegistry.end())
            return CCU_STATUS_INVALID_OBJECT;
        context = it->second;
    }
    // HComm backend contexts start with the single-rank default. The launch
    // schedule is the authoritative source for distributed rank topology.
    if (schd.rankSize != 0) {
        if (schd.rankId >= schd.rankSize)
            return CCU_STATUS_INVALID_ARGUMENT;
        context->rank = schd.rankId;
        context->rankSize = schd.rankSize;
    } else if (schd.rankId != 0) {
        if (schd.rankId >= context->rankSize)
            return CCU_STATUS_INVALID_ARGUMENT;
        context->rank = schd.rankId;
    }
    return ccu_compiler_submit_with_schedule(
        context, descriptor, schd, stream, arguments, argumentCount, externalArguments, externalArgumentCount);
}

ccu_status_t ccu_synchronize(ccu_context_t* context, void* stream, uint64_t timeoutMs)
{
    if (!context || !stream)
        return CCU_STATUS_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(context->mutex);
        if (!context->pendingLaunch)
            return CCU_STATUS_SUCCESS;
        if (context->pendingLaunch->stream != stream)
            return CCU_STATUS_INVALID_ARGUMENT;

        ccu_launch* launch = context->pendingLaunch;
        const ccu_status_t syncStatus = synchronizeLocked(launch, timeoutMs);
        if (!launch->terminal)
            return syncStatus;
        const CleanupResult cleanup = cleanupLaunchLocked(launch);
        if (!cleanup.released)
            return cleanup.status;
        return syncStatus;
    } catch (...) {
        return CCU_STATUS_RUNTIME_ERROR;
    }
}

const char* ccu_status_string(ccu_status_t status)
{
    switch (status) {
        case CCU_STATUS_SUCCESS:
            return "success";
        case CCU_STATUS_INVALID_ARGUMENT:
            return "invalid argument";
        case CCU_STATUS_VERSION_MISMATCH:
            return "version mismatch";
        case CCU_STATUS_INVALID_OBJECT:
            return "invalid object";
        case CCU_STATUS_KERNEL_NOT_FOUND:
            return "kernel not found";
        case CCU_STATUS_SCHEMA_MISMATCH:
            return "schema mismatch";
        case CCU_STATUS_RESOURCE_EXHAUSTED:
            return "resource exhausted";
        case CCU_STATUS_RUNTIME_ERROR:
            return "runtime error";
        case CCU_STATUS_TIMEOUT_RECOVERED:
            return "timeout recovered";
        case CCU_STATUS_RECOVERY_FAILED:
            return "recovery failed";
        case CCU_STATUS_BUSY:
            return "busy";
        case CCU_STATUS_CONTEXT_POISONED:
            return "context poisoned";
    }
    return "unknown status";
}

} // extern "C"
#pragma GCC visibility pop
