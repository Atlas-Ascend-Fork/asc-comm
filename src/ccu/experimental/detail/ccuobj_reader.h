/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
//
// Authoritative, hcomm-free `.ccuobj` reader.
//
// The `.ccuobj` format is defined by this repo ; its parsing
// must not be reimplemented per-consumer. This header is the single source of
// truth for turning a `.ccuobj` blob into section/kernel/manifest/relocation
// views. Both consumers use it verbatim:
//   - this repo's AOT adapter (lib/Runtime/ccu_aot_adapter.cpp)
//   - hcomm's official loader (vendored alongside CcuRelocFormat.h/RelocApply.h)
//
// It is deliberately hcomm-free: raw fixed-width byte reads + the reloc structs
// from CcuRelocFormat.h, zero CcuInstr / hcomm dependency. Relocation entries
// are returned as zero-copy pointers into the caller's blob (the blob must
// outlive the ParsedModule). On-disk layout mirrors the writer's CcuobjFormat.h
// and the on-disk layout; offsets are bounds-checked (malformed → parse fails
// or the optional section is dropped, never an out-of-bounds read).
//
//===----------------------------------------------------------------------===//

#ifndef CCU_LOADER_CCUOBJ_READER_H
#define CCU_LOADER_CCUOBJ_READER_H

#include "ccu_reloc_format.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ccu::ccuobj {

/// One kernel's parsed views. Pointers reference the caller-owned blob.
struct ParsedKernel {
    const char* name = nullptr; // into StringTable (not NUL-terminated)
    uint32_t nameLen = 0;
    const uint8_t* microcode = nullptr; // CcuInstr[] (instrCount * 32 bytes)
    uint32_t instrCount = 0;
    uint32_t channelArgCount = 0;
    uint32_t taskArgCount = 0;
    uint32_t missionStartInstrId = 0;
    uint32_t missionInstrCount = 0;
    // ResourceManifest (ccu-allocate-resources per-class 0-based counts).
    uint32_t varCount = 0;
    uint32_t eventCount = 0;
    uint32_t localAddrCount = 0;
    uint32_t remoteAddrCount = 0;
    // MS/CcuBuf scratch pool count, from manifest `reserved` low-32.
    // Kernels without CcuBuf ops keep this zero; kernels with CcuBuf create ops
    // are materialised by the runtime via CreateBlockCcuBuf(msCount, ...).
    uint32_t msCount = 0;
    // LoopEngine executor pool count, from manifest `reserved` high-32
    // Kernels with executor create ops are materialised by the
    // runtime via CreateBlockExecutor(executorCount, ...).
    uint32_t executorCount = 0;
    // RELOCATION slice for this kernel (zero-copy into blob). Empty
    // when the .ccuobj predates the section or it is malformed.
    const RelocEntry* relocs = nullptr;
    size_t relocCount = 0;
};

/// A parsed `.ccuobj` module: header facts + every kernel's views.
struct ParsedModule {
    uint32_t version = 0;
    uint32_t abiRev = 0;
    const char* moduleName = nullptr;
    uint32_t moduleNameLen = 0;
    std::vector<ParsedKernel> kernels;
};

namespace detail {

// On-disk constants. Section bodies are
// located by the section table; field offsets within each struct are fixed.
enum : uint32_t {
    kSecSymbolTable = 0x01,
    kSecKernelTable = 0x02,
    kSecMicrocode = 0x03,
    kSecResourceManifest = 0x05,
    kSecStringTable = 0x06,
    kSecRelocation = 0x08,
};
constexpr size_t kCcuInstrSize = 32u;
constexpr size_t kSectionEntrySize = 24u; // type@0 flags@4 off@8 size@16
constexpr size_t kHeaderSize = 64;
constexpr size_t kMagicSize = 8;
constexpr size_t kVersionOffset = 8;
constexpr size_t kAbiRevisionOffset = 12;
constexpr size_t kModuleNameOffset = 32;
constexpr size_t kModuleNameLengthOffset = 40;
constexpr size_t kSectionTableOffset = 48;
constexpr size_t kSectionCountOffset = 56;
constexpr size_t kKernelEntrySize = 64;
constexpr size_t kSymbolEntrySize = 24; // nameOff@0 nameLen@8 kernelIdx@16
constexpr size_t kKernelChannelArgCountOffset = 48;
constexpr size_t kKernelTaskArgCountOffset = 52;
constexpr size_t kKernelMissionStartOffset = 56;
constexpr size_t kKernelMissionCountOffset = 60;
constexpr size_t kResourceManifestEntrySize = 24;
constexpr size_t kResourceEventCountOffset = 4;
constexpr size_t kResourceLocalAddrCountOffset = 8;
constexpr size_t kResourceRemoteAddrCountOffset = 12;
constexpr size_t kResourceMsCountOffset = 16;
constexpr size_t kResourceExecutorCountOffset = 20;

// Bounds: is [off, off+len) within [0, limit)? Overflow-safe for an *arbitrary*
// `off`/`len` because it never adds them (the short-circuit guards the
// subtraction). Callers must therefore pass a *single* file-derived offset, not
// a precomputed `sectionOff + relativeOff` sum — that addition can wrap a
// crafted relative offset back into range. To stay overflow-safe, bound a
// section-relative offset against the *section size* (already verified within
// the blob): if `relOff + len <= sectionSize` and `sectionOff + sectionSize <=
// blobSize`, then `sectionOff + relOff + len <= blobSize` with no wrap.
inline bool inBounds(uint64_t off, uint64_t len, size_t limit)
{
    return off <= limit && len <= static_cast<uint64_t>(limit) - off;
}

template <typename T>
inline T rd(const uint8_t* p)
{ // unaligned little-endian read
    T v;
    const auto* src = static_cast<const unsigned char*>(p);
    auto* dst = reinterpret_cast<unsigned char*>(&v);
    std::copy(src, src + sizeof(T), dst);
    return v;
}

} // namespace detail

/// Parse a `.ccuobj` blob into `out`. Returns false on malformed/foreign input
/// (bad magic, truncated, required section missing/out-of-bounds). The optional
/// RELOCATION section is dropped (kernels keep empty reloc slices) for legacy
/// revisions, while abiRev 4 requires a structurally valid v4 section and
/// selector metadata. `out.kernels[i].*` pointers reference `data`, which must
/// outlive `out`. Header-only + inline so consumers (incl. hcomm vendored) need
/// no extra translation unit. The reader validates the v4 relocation contract;
/// callers still decide whether a reported abiRev is supported.
inline bool parseCcuobj(const void* data, size_t size, ParsedModule& out, const char** error = nullptr)
{
    using namespace detail;
    auto fail = [&](const char* reason) {
        if (error)
            *error = reason;
        return false;
    };
    if (!data || size < kHeaderSize)
        return fail("header-truncated");
    const auto* base = static_cast<const uint8_t*>(data);
    if (std::memcmp(base, "CCUOBJ\0\0", kMagicSize) != 0)
        return fail("bad-magic");
    out.version = rd<uint32_t>(base + kVersionOffset);
    out.abiRev = rd<uint32_t>(base + kAbiRevisionOffset);
    const uint64_t modNameOff = rd<uint64_t>(base + kModuleNameOffset);
    const uint64_t modNameLen = rd<uint64_t>(base + kModuleNameLengthOffset);
    const uint64_t secOff = rd<uint64_t>(base + kSectionTableOffset);
    const uint32_t secCnt = rd<uint32_t>(base + kSectionCountOffset);
    if (!inBounds(secOff, static_cast<uint64_t>(secCnt) * kSectionEntrySize, size))
        return fail("section-table-out-of-bounds");

    // Collect section bodies (off + size), keyed by type.
    uint64_t ktOff = 0, ktSz = 0, mcOff = 0, mcSz = 0, rmOff = 0, rmSz = 0;
    uint64_t strOff = 0, strSz = 0, symOff = 0, symSz = 0, relOff = 0, relSz = 0;
    bool haveKT = false, haveMC = false, haveRM = false, haveStr = false, haveSym = false, haveRel = false;
    for (uint32_t i = 0; i < secCnt; ++i) {
        const uint8_t* s = base + secOff + i * kSectionEntrySize;
        const uint32_t type = rd<uint32_t>(s);
        const uint64_t off = rd<uint64_t>(s + 8);
        const uint64_t sz = rd<uint64_t>(s + 16);
        switch (type) {
            case kSecSymbolTable:
                symOff = off;
                symSz = sz;
                haveSym = true;
                break;
            case kSecKernelTable:
                ktOff = off;
                ktSz = sz;
                haveKT = true;
                break;
            case kSecMicrocode:
                mcOff = off;
                mcSz = sz;
                haveMC = true;
                break;
            case kSecResourceManifest:
                rmOff = off;
                rmSz = sz;
                haveRM = true;
                break;
            case kSecStringTable:
                strOff = off;
                strSz = sz;
                haveStr = true;
                break;
            case kSecRelocation:
                relOff = off;
                relSz = sz;
                haveRel = true;
                break;
            default:
                break; // tolerate unknown sections (0x40-0xFF vendor range)
        }
    }
    if (!haveKT || !haveMC)
        return fail("required-section-missing");
    if (!inBounds(ktOff, ktSz, size) || !inBounds(mcOff, mcSz, size))
        return fail("kernel-or-microcode-out-of-bounds");
    if (haveRM && !inBounds(rmOff, rmSz, size))
        return fail("resource-manifest-out-of-bounds");
    if (haveStr && !inBounds(strOff, strSz, size))
        return fail("string-table-out-of-bounds");
    if (haveSym && !inBounds(symOff, symSz, size))
        return fail("symbol-table-out-of-bounds");
    if (haveRel && !inBounds(relOff, relSz, size)) {
        haveRel = false;
    }

    // Module name (StringTable-relative; bound against the section size so the
    // strOff+modNameOff sum cannot overflow past the blob).
    if (haveStr && inBounds(modNameOff, modNameLen, strSz)) {
        out.moduleName = reinterpret_cast<const char*>(base + strOff + modNameOff);
        out.moduleNameLen = static_cast<uint32_t>(modNameLen);
    }

    const size_t kernCount = static_cast<size_t>(ktSz / kKernelEntrySize);
    out.kernels.clear();
    out.kernels.resize(kernCount);

    // Pre-decode the relocation section header once (shared across kernels).
    bool relUsable = false;
    uint32_t relKernelCount = 0;
    const uint8_t* relEntries = nullptr; // base of RelocEntry[] array
    uint64_t relMaxEntries = 0;          // entry slots after the headers
    if (haveRel && relSz >= sizeof(RelocSectionHeader)) {
        const uint8_t* rs = base + relOff;
        relKernelCount = rd<uint32_t>(rs + 0);
        const uint32_t entrySize = rd<uint32_t>(rs + 4);
        const uint64_t totalEntryCount = rd<uint64_t>(rs + 8);
        const uint64_t hdrsEnd =
            sizeof(RelocSectionHeader) + static_cast<uint64_t>(relKernelCount) * sizeof(RelocKernelHeader);
        if (entrySize == sizeof(RelocEntry) && hdrsEnd <= relSz) {
            relEntries = rs + hdrsEnd;
            const uint64_t availableEntries = (relSz - hdrsEnd) / sizeof(RelocEntry);
            if (totalEntryCount <= availableEntries) {
                relMaxEntries = totalEntryCount;
                relUsable = true;
            }
        }
    }
    if (out.abiRev == kRelocAbiRev && (!haveRel || !relUsable || relKernelCount != kernCount))
        return fail("relocation-section-invalid");

    for (size_t i = 0; i < kernCount; ++i) {
        const uint8_t* ke = base + ktOff + i * kKernelEntrySize;
        ParsedKernel& k = out.kernels[i];
        // KernelEntry: microcodeOff@0 (Microcode-section-relative), microcodeLen@8,
        // resourceManifestOff@32, logical channel-table width@48,
        // taskArgCount@52,
        // missionStartInstrId@56, missionInstrCount@60.
        const uint64_t mcRel = rd<uint64_t>(ke + 0);
        const uint64_t mcLen = rd<uint64_t>(ke + 8);
        const uint64_t rmRel = rd<uint64_t>(ke + 32);
        if (inBounds(mcRel, mcLen, mcSz)) {
            k.microcode = base + mcOff + mcRel;
            k.instrCount = static_cast<uint32_t>(mcLen / kCcuInstrSize);
        }
        k.channelArgCount = rd<uint32_t>(ke + kKernelChannelArgCountOffset);
        k.taskArgCount = rd<uint32_t>(ke + kKernelTaskArgCountOffset);
        k.missionStartInstrId = rd<uint32_t>(ke + kKernelMissionStartOffset);
        k.missionInstrCount = rd<uint32_t>(ke + kKernelMissionCountOffset);

        if (haveRM && inBounds(rmRel, kResourceManifestEntrySize, rmSz)) {
            const uint8_t* rm = base + rmOff + rmRel;
            k.varCount = rd<uint32_t>(rm + 0);
            k.eventCount = rd<uint32_t>(rm + kResourceEventCountOffset);
            k.localAddrCount = rd<uint32_t>(rm + kResourceLocalAddrCountOffset);
            k.remoteAddrCount = rd<uint32_t>(rm + kResourceRemoteAddrCountOffset);
            // reserved@16 (u64): low-32 = MS/CcuBuf pool count,
            // high-32 = LoopEngine executor count.
            k.msCount = rd<uint32_t>(rm + kResourceMsCountOffset);
            k.executorCount = rd<uint32_t>(rm + kResourceExecutorCountOffset);
        }

        // RELOCATION slice for kernel i: RelocKernelHeader[i] → entry range.
        if (relUsable && i < relKernelCount) {
            const uint8_t* rkh = base + relOff + sizeof(RelocSectionHeader) + i * sizeof(RelocKernelHeader);
            const uint64_t entryOff = rd<uint64_t>(rkh + 0);
            const uint64_t entryCount = rd<uint64_t>(rkh + 8);
            // Bound the index-space slice against the entry region, overflow-safe:
            // entryOff..entryOff+entryCount must fit in relMaxEntries, so the byte
            // offset entryOff*sizeof(RelocEntry) cannot wrap past the blob.
            if (entryOff <= relMaxEntries && entryCount <= relMaxEntries - entryOff) {
                k.relocs = reinterpret_cast<const RelocEntry*>(relEntries + entryOff * sizeof(RelocEntry));
                k.relocCount = static_cast<size_t>(entryCount);
                for (size_t ri = 0; ri < k.relocCount; ++ri) {
                    if (!hasValidChannelSelector(k.relocs[ri], k.channelArgCount))
                        return fail("relocation-selector-invalid");
                }
            } else if (out.abiRev == kRelocAbiRev) {
                return fail("relocation-kernel-slice-invalid");
            }
        }
    }

    // Kernel names from SymbolTable (SymbolEntry: nameOff@0 nameLen@8 kernelIdx@16).
    if (haveSym && haveStr) {
        const size_t symCount = static_cast<size_t>(symSz / kSymbolEntrySize);
        for (size_t i = 0; i < symCount; ++i) {
            const uint8_t* se = base + symOff + i * kSymbolEntrySize;
            const uint64_t nameOff = rd<uint64_t>(se + 0);
            const uint64_t nameLen = rd<uint64_t>(se + 8);
            const uint32_t kIdx = rd<uint32_t>(se + 16);
            if (kIdx < kernCount && inBounds(nameOff, nameLen, strSz)) {
                out.kernels[kIdx].name = reinterpret_cast<const char*>(base + strOff + nameOff);
                out.kernels[kIdx].nameLen = static_cast<uint32_t>(nameLen);
            }
        }
    }
    return true;
}

} // namespace ccu::ccuobj

#endif // CCU_LOADER_CCUOBJ_READER_H
