/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_PRINT_HPP
#define ASCCOMM_CCU_PRINT_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

#include "ccu/hcomm/ccu_primitives.hpp"

namespace AscendC {
namespace ccu {
namespace dfx {

namespace detail {

// Host metadata only. The CCU program loads the ring address from this GM cell;
// it never dereferences this C++ object, its strings, or the Host stack.
struct PrintInfoHandle {
    uint64_t addressCell;
    uint64_t sequenceAddressCell;
    uint64_t recordBytes;
    uint64_t ringBytes;
    const char* wrapLabel;
    const char* spaceLabel;
    const char* fullLabel;
};

template <typename... Args>
struct AreVariables : std::conjunction<std::is_same<typename std::decay<Args>::type, variable>...> {};

inline CcuResult StoreArgs(variable&, const variable&) { return CCU_SUCCESS; }

template <typename First, typename... Rest>
CcuResult StoreArgs(variable& address, const variable& wordBytes, const First& first, const Rest&... rest)
{
    const CcuResult result = store(address, first);
    if (result != CCU_SUCCESS) {
        return result;
    }
    address += wordBytes;
    return StoreArgs(address, wordBytes, rest...);
}

// Emit both edges explicitly so failure to build a condition cannot silently
// skip its body and leave an apparently successful, incomplete print program.
template <typename Body>
CcuResult EmitIf(const variable& lhs, uint64_t rhs, ccu_condition_type condition, const char* label, Body body)
{
    CcuResult result = ::asc::ccu_if_begin(lhs.handle, rhs, condition, label);
    if (result != CCU_SUCCESS) {
        return result;
    }
    try {
        result = body();
    } catch (...) {
        // Variable operators can throw while emitting instructions. Balance the
        // builder's condition stack, then preserve the original build failure.
        (void)::asc::ccu_if_end(label);
        throw;
    }
    const CcuResult endResult = ::asc::ccu_if_end(label);
    return result == CCU_SUCCESS ? endResult : result;
}

} // namespace detail

class CcuPrintStream {
public:
    static CcuPrintStream* GetInstance();

    // Each emission gets its own ring, even when two kernels use the same source
    // line. Allocate under the ACL context that owns the kernel being built.
    detail::PrintInfoHandle* AllocPrintInfo(const char* filename, int lineno, const char* fmt, size_t argc);

    // Automatic reclamation needs coordinated kernel-unregister/last-mission
    // lifecycle support from the control plane (hcomm). Until then no ring is
    // reclaimed on kernel builder return: emitted programs still contain its GM
    // address. The owner must synchronize ALL users before explicit Release.
    // Flush consumes available records, not unfinished CCU work. Stop only joins
    // the reader; rings remain valid for already emitted programs. Release must
    // be called AFTER all referencing missions finish and will invalidate them.
    void Flush();
    void Stop();
    void Release();

    ~CcuPrintStream();
    CcuPrintStream(const CcuPrintStream&) = delete;
    CcuPrintStream& operator=(const CcuPrintStream&) = delete;

private:
    CcuPrintStream();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

template <typename... Args>
CcuResult EmitPrintf(const detail::PrintInfoHandle* handle, const Args&... args)
{
    static_assert(detail::AreVariables<Args...>::value, "CCU_PRINTF arguments must be ccu::variable");
    if (handle == nullptr) {
        return CCU_SUCCESS; // Registration failure disables this debug call, not communication.
    }

    variable base, tail, head, next, cursor, word, sequence;
    CcuResult result = load(handle->addressCell, base);
    if (result != CCU_SUCCESS) {
        return result;
    }
    result = load(base, tail);
    if (result != CCU_SUCCESS) {
        return result;
    }
    word = sizeof(uint64_t);
    cursor = base + word;
    result = load(cursor, head);
    if (result != CCU_SUCCESS) {
        return result;
    }
    word = handle->recordBytes;
    next = tail + word;
    result = detail::EmitIf(next, handle->ringBytes, ccu_condition_eq, handle->wrapLabel, [&]() {
        next = uint64_t{0};
        return CCU_SUCCESS;
    });
    if (result != CCU_SUCCESS) {
        return result;
    }

    // Shared per ACL context, not atomic: concurrent missions can read the same
    // sequence or overwrite a newer value. It is only an ordering hint, not a fence.
    result = load(handle->sequenceAddressCell, cursor);
    if (result != CCU_SUCCESS) {
        return result;
    }
    result = load(cursor, sequence);
    if (result != CCU_SUCCESS) {
        return result;
    }
    word = uint64_t{1};
    sequence += word;
    result = store(cursor, sequence);
    if (result != CCU_SUCCESS) {
        return result;
    }

    // Keep one of the 32 slots empty (31 usable). Drop rather than overwrite an
    // unread record or stall communication. Each ring requires a single writer.
    // A5 cannot compare two variables (nor subtract/XOR them). The Host stores
    // head as 0 - byteOffset, so modular addition tests equality against zero.
    // Compute once: the full branch may reuse other temporaries.
    head = next + head;
    result = detail::EmitIf(head, uint64_t{0}, ccu_condition_eq, handle->fullLabel, [&]() -> CcuResult {
        word = uint64_t{2 * sizeof(uint64_t)};
        cursor = base + word;
        CcuResult status = load(cursor, sequence);
        if (status != CCU_SUCCESS) {
            return status;
        }
        word = uint64_t{1};
        sequence += word;
        return store(cursor, sequence); // Per-ring dropped-record count.
    });
    if (result != CCU_SUCCESS) {
        return result;
    }
    return detail::EmitIf(head, uint64_t{0}, ccu_condition_ne, handle->spaceLabel, [&]() -> CcuResult {
        word = uint64_t{3 * sizeof(uint64_t)};
        cursor = base + word;
        cursor += tail;
        CcuResult writeResult = store(cursor, sequence);
        if (writeResult != CCU_SUCCESS) {
            return writeResult;
        }
        word = sizeof(uint64_t);
        cursor += word;
        writeResult = detail::StoreArgs(cursor, word, args...);
        if (writeResult != CCU_SUCCESS) {
            return writeResult;
        }
        // Each CCU store waits for its transfer before the next instruction.
        // Publish tail after payload; the Host reads tail BEFORE copying payload
        // and advances head only after owning a complete copy of every record.
        return store(base, next);
    });
}

template <typename... Args>
CcuResult RegisterAndEmit(const char* file, int line, const char* fmt, const Args&... args)
{
    static_assert(detail::AreVariables<Args...>::value, "CCU_PRINTF arguments must be ccu::variable");
    return EmitPrintf(CcuPrintStream::GetInstance()->AllocPrintInfo(file, line, fmt, sizeof...(args)), args...);
}

// Returns status without returning from the caller. All arguments are runtime
// ccu::variable values; %u/%v print unsigned decimal, %x hexadecimal, %p 0x-prefixed
// hexadecimal, and %% a literal percent. No width/precision modifiers are used.
// Uses software IF blocks; do not emit inside a hardware ccu::loop body.
// Instruction generation runs on the Host. One emitted call must have one
// runtime writer: the same program must not run concurrently on several missions.
#define CCU_PRINTF(fmt, ...) ::AscendC::ccu::dfx::RegisterAndEmit(__FILE__, __LINE__, (fmt), ##__VA_ARGS__)

} // namespace dfx
} // namespace ccu
} // namespace AscendC

#endif // ASCCOMM_CCU_PRINT_HPP
