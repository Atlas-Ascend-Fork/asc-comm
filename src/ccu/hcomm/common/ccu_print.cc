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
#include <algorithm>
#include <map>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

namespace AscendC {
namespace ccu {
namespace dfx {
namespace {

// Use the existing module but preserve run-log classification: printf records
// must not accidentally become debug-only HCCL_INFO through compatibility macros.
void Log(int level, const char* message)
{
    if (DlogRecord != nullptr) {
        DlogRecord(HCCL | RUN_LOG_MASK, level, "[CCU_PRINT] %s", message);
    }
}

bool Check(aclError error, const char* operation)
{
    if (error == ACL_SUCCESS) {
        return true;
    }
    if (DlogRecord != nullptr) {
        DlogRecord(HCCL | RUN_LOG_MASK, DLOG_WARN, "[CCU_PRINT] %s failed, aclError[%d]", operation, error);
    }
    return false;
}

} // namespace

namespace detail {

bool ValidateFormat(const char* fmt, size_t argc)
{
    if (fmt == nullptr) {
        return false;
    }
    size_t count = 0;
    for (const char* p = fmt; *p != '\0'; ++p) {
        if (*p != '%') {
            continue;
        }
        ++p;
        if (*p == '%') {
            continue;
        }
        if (*p != 'u' && *p != 'x' && *p != 'p' && *p != 'v') {
            return false;
        }
        ++count;
    }
    return count == argc;
}

bool Format(const char* fmt, const uint64_t* argv, size_t argc, std::string& output)
{
    if (!ValidateFormat(fmt, argc) || (argc != 0 && argv == nullptr)) {
        return false;
    }
    size_t index = 0;
    for (const char* p = fmt; *p != '\0'; ++p) {
        if (*p != '%') {
            output.push_back(*p);
            continue;
        }
        ++p;
        if (*p == '%') {
            output.push_back('%');
            continue;
        }
        if (*p == 'p') {
            output += "0x";
        }
        const uint64_t base = (*p == 'x' || *p == 'p') ? 16 : 10;
        uint64_t value = argv[index++];
        char buffer[64];
        char* cursor = buffer + sizeof(buffer);
        do {
            *--cursor = "0123456789abcdef"[value % base];
            value /= base;
        } while (value != 0);
        output.append(cursor, buffer + sizeof(buffer));
    }
    return true;
}

} // namespace detail

struct CcuPrintStream::Impl {
    struct Entry {
        detail::PrintInfoHandle handle{};
        aclrtContext context{nullptr};
        void* ring{nullptr};
        void* addressCell{nullptr};
        size_t argc{0};
        size_t allocationBytes{0};
        std::string file;
        int line{0};
        std::string format;
        std::string wrapLabel;
        std::string spaceLabel;
        std::string fullLabel;
        uint64_t lastDropped{0};
        std::vector<uint64_t> snapshot;

        ~Entry()
        {
            // Never free a device allocation under a different device/context.
            // Explicit Release before context destruction is the supported path.
            aclrtContext previous = nullptr;
            (void)aclrtGetCurrentContext(&previous);
            if (aclrtSetCurrentContext(context) == ACL_SUCCESS) {
                if (addressCell != nullptr) {
                    (void)Check(aclrtFree(addressCell), "aclrtFree address cell");
                }
                if (ring != nullptr) {
                    (void)Check(aclrtFree(ring), "aclrtFree ring");
                }
            } else {
                Log(DLOG_WARN, "context unavailable during release; device allocations were not freed");
            }
            if (previous != nullptr) {
                (void)aclrtSetCurrentContext(previous);
            }
        }
    };

    std::mutex mutex;
    std::condition_variable wakeup;
    std::vector<std::unique_ptr<Entry>> entries;
    std::map<aclrtContext, std::unique_ptr<Entry>> sequences;
    struct Pending {
        uint64_t sequence;
        std::string text;
    };
    std::thread consumer;
    bool running{false};

    void Consume(Entry& entry, std::vector<Pending>& pending)
    {
        detail::PrintRingHeader header{};
        if (!Check(aclrtSetCurrentContext(entry.context), "aclrtSetCurrentContext") ||
            !Check(
                aclrtMemcpy(&header, sizeof(header), entry.ring, sizeof(header), ACL_MEMCPY_DEVICE_TO_HOST),
                "aclrtMemcpy header")) {
            return;
        }
        const uint64_t originalHead = uint64_t{0} - header.head;
        uint64_t head = originalHead;
        const uint64_t tail = header.tail;
        if (head >= entry.handle.ringBytes || tail >= entry.handle.ringBytes || head % entry.handle.recordBytes != 0 ||
            tail % entry.handle.recordBytes != 0) {
            Log(DLOG_WARN, "invalid print ring cursor");
            return;
        }
        if (header.dropped != entry.lastDropped) {
            const std::string warning = entry.file + ":" + std::to_string(entry.line) +
                                        " dropped=" + std::to_string(header.dropped - entry.lastDropped) +
                                        " records; ring depth=32 (31 usable); increase depth if needed";
            Log(DLOG_WARN, warning.c_str());
            entry.lastDropped = header.dropped;
        }
        // Read publication first, then payload. A single bulk DMA of header and
        // payload would not guarantee that the copied payload follows that tail.
        while (head != tail) {
            auto* source = static_cast<uint8_t*>(entry.ring) + sizeof(header) + head;
            if (!Check(
                    aclrtMemcpy(
                        entry.snapshot.data(), entry.handle.recordBytes, source, entry.handle.recordBytes,
                        ACL_MEMCPY_DEVICE_TO_HOST),
                    "aclrtMemcpy record")) {
                break;
            }
            std::string output = entry.file + ":" + std::to_string(entry.line) + " ";
            if (!detail::Format(entry.format.c_str(), entry.snapshot.data() + 1, entry.argc, output)) {
                Log(DLOG_WARN, "invalid print record format");
                break;
            }
            pending.push_back({entry.snapshot[0], std::move(output)});
            head = (head + entry.handle.recordBytes) % entry.handle.ringBytes;
        }
        if (head != originalHead) {
            const uint64_t encodedHead = uint64_t{0} - head;
            (void)Check(
                aclrtMemcpy(
                    static_cast<uint8_t*>(entry.ring) + offsetof(detail::PrintRingHeader, head), sizeof(encodedHead),
                    &encodedHead, sizeof(encodedHead), ACL_MEMCPY_HOST_TO_DEVICE),
                "aclrtMemcpy head");
        }
    }

    bool ConsumeAll()
    {
        // The lock serializes registration, Flush and the reader. Sorting only
        // orders this scan's records, not late records from a different scan.
        // Counters are per context; no ordering is claimed between devices.
        aclrtContext previous = nullptr;
        (void)aclrtGetCurrentContext(&previous);
        bool active = false;
        for (const auto& context : sequences) {
            std::vector<Pending> pending;
            for (const auto& entry : entries) {
                if (entry->context != context.first) {
                    continue;
                }
                try {
                    Consume(*entry, pending);
                } catch (...) {
                    Log(DLOG_WARN, "Host print consumer failed; record may be retried");
                }
            }
            std::stable_sort(pending.begin(), pending.end(), [](const Pending& a, const Pending& b) {
                return a.sequence < b.sequence;
            });
            for (const auto& record : pending) {
                Log(DLOG_INFO, record.text.c_str());
            }
            active |= !pending.empty();
        }
        if (previous != nullptr) {
            (void)aclrtSetCurrentContext(previous);
        }
        return active;
    }

    Entry* GetSequence(aclrtContext context)
    {
        auto found = sequences.find(context);
        if (found != sequences.end()) {
            return found->second.get();
        }
        auto entry = std::make_unique<Entry>();
        entry->context = context;
        if (!Check(aclrtMalloc(&entry->ring, sizeof(uint64_t), ACL_MEM_MALLOC_HUGE_FIRST), "aclrtMalloc sequence") ||
            !Check(
                aclrtMalloc(&entry->addressCell, sizeof(uint64_t), ACL_MEM_MALLOC_HUGE_FIRST),
                "aclrtMalloc sequence cell") ||
            !Check(aclrtMemset(entry->ring, sizeof(uint64_t), 0, sizeof(uint64_t)), "aclrtMemset sequence")) {
            return nullptr;
        }
        const uint64_t address = reinterpret_cast<uint64_t>(entry->ring);
        if (!Check(
                aclrtMemcpy(entry->addressCell, sizeof(address), &address, sizeof(address), ACL_MEMCPY_HOST_TO_DEVICE),
                "aclrtMemcpy sequence cell")) {
            return nullptr;
        }
        auto* result = entry.get();
        sequences.emplace(context, std::move(entry));
        return result;
    }

    void StartLocked()
    {
        if (running) {
            return;
        }
        consumer = std::thread([this] {
            std::unique_lock<std::mutex> lock(mutex);
            while (running) {
                const bool active = ConsumeAll();
                // Poll at 100 us while draining, 1 ms when idle. This is a polling
                // budget, not a promise about DMA or log delivery latency.
                wakeup.wait_for(lock, std::chrono::microseconds(active ? 100 : 1000), [this] { return !running; });
            }
            ConsumeAll();
        });
        running = true; // Worker must acquire mutex before reading this flag.
    }
};

CcuPrintStream* CcuPrintStream::GetInstance()
{
    static CcuPrintStream instance;
    return &instance;
}

CcuPrintStream::CcuPrintStream() : impl_(new Impl) {}
CcuPrintStream::~CcuPrintStream() { Release(); }

detail::PrintInfoHandle* CcuPrintStream::AllocPrintInfo(const char* file, int line, const char* fmt, size_t argc)
{
    if (file == nullptr || !detail::ValidateFormat(fmt, argc)) {
        Log(DLOG_WARN, "invalid format or argument count; print disabled");
        return nullptr;
    }
    if (argc == std::numeric_limits<size_t>::max()) {
        return nullptr;
    }
    const size_t words = argc + 1; // Sequence word, then the runtime arguments.
    if (words > (std::numeric_limits<size_t>::max() - sizeof(detail::PrintRingHeader)) / sizeof(uint64_t) /
                    detail::PRINT_RING_DEPTH) {
        Log(DLOG_WARN, "print allocation size overflow");
        return nullptr;
    }
    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        std::unique_ptr<Impl::Entry> entry(new Impl::Entry);
        if (!Check(aclrtGetCurrentContext(&entry->context), "aclrtGetCurrentContext") || entry->context == nullptr) {
            return nullptr;
        }
        auto* sequence = impl_->GetSequence(entry->context);
        if (sequence == nullptr) {
            return nullptr;
        }
        entry->handle.sequenceAddressCell = reinterpret_cast<uint64_t>(sequence->addressCell);
        entry->file = file;
        entry->line = line;
        entry->format = fmt;
        entry->argc = argc;
        entry->handle.recordBytes = words * sizeof(uint64_t);
        entry->handle.ringBytes = entry->handle.recordBytes * detail::PRINT_RING_DEPTH;
        entry->allocationBytes = sizeof(detail::PrintRingHeader) + entry->handle.ringBytes;
        entry->snapshot.resize(words);
        if (!Check(aclrtMalloc(&entry->ring, entry->allocationBytes, ACL_MEM_MALLOC_HUGE_FIRST), "aclrtMalloc ring") ||
            !Check(aclrtMalloc(&entry->addressCell, sizeof(uint64_t), ACL_MEM_MALLOC_HUGE_FIRST), "aclrtMalloc cell") ||
            !Check(aclrtMemset(entry->ring, entry->allocationBytes, 0, entry->allocationBytes), "aclrtMemset ring")) {
            return nullptr;
        }
        const uint64_t address = reinterpret_cast<uint64_t>(entry->ring);
        if (!Check(
                aclrtMemcpy(entry->addressCell, sizeof(address), &address, sizeof(address), ACL_MEMCPY_HOST_TO_DEVICE),
                "aclrtMemcpy address cell")) {
            return nullptr;
        }
        entry->handle.addressCell = reinterpret_cast<uint64_t>(entry->addressCell);
        entry->wrapLabel = "ccu.printf.wrap." + std::to_string(address);
        entry->spaceLabel = "ccu.printf.space." + std::to_string(address);
        entry->handle.wrapLabel = entry->wrapLabel.c_str();
        entry->handle.spaceLabel = entry->spaceLabel.c_str();
        entry->fullLabel = "ccu.printf.full." + std::to_string(address);
        entry->handle.fullLabel = entry->fullLabel.c_str();
        impl_->entries.push_back(std::move(entry));
        impl_->StartLocked();
        return &impl_->entries.back()->handle;
    } catch (...) {
        Log(DLOG_WARN, "print registration failed; print disabled");
        return nullptr;
    }
}

void CcuPrintStream::Flush()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ConsumeAll();
}

void CcuPrintStream::Stop()
{
    // Call lifecycle methods from one controlling Host thread, not concurrently
    // with registration. Do not hold mutex while joining the worker.
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->running = false;
        impl_->wakeup.notify_all();
    }
    if (impl_->consumer.joinable()) {
        impl_->consumer.join();
    }
}

void CcuPrintStream::Release()
{
    Stop();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->entries.clear();
    impl_->sequences.clear();
}

} // namespace dfx
} // namespace ccu
} // namespace AscendC
