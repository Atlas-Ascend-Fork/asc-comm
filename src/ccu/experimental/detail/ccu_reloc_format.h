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
// RELOCATION section structures and pool/kind enums.
//
// Split out of CcuobjFormat.h so consumers can use the relocation table types
// and table-driven applier without pulling in instruction layout definitions.
// These structs are fixed-width integers with no CcuInstr dependency.
//
// CcuobjFormat.h includes this header, so the `ccu::ccuobj` reloc symbols stay
// in their original namespace and every existing include keeps working.
//
//===----------------------------------------------------------------------===//

#ifndef CCU_WRITER_CCU_RELOC_FORMAT_H
#define CCU_WRITER_CCU_RELOC_FORMAT_H

#include <cstdint>

namespace ccu::ccuobj {

// - Relocation section -------------------------------------------

inline constexpr uint32_t kRelocAbiRev = 4u;

/// The writer emits microcode with *pool-local 0-indexed* resource ids
/// (xn/gsa/cke/ms) and *logical* channel / notify indices. The runtime
/// allocates each pool as a contiguous physical segment whose base is
/// generally non-zero, and resolves physical channel/notify ids only at
/// launch. A loader must therefore relocate every id-typed field before
/// executing the microcode.
///
/// The writer emits a table of `RelocEntry` describing each resource field.
/// Loaders apply entries mechanically by reading `byteOff`, selecting the
/// physical base for the pool, and applying the relocation kind.
///
/// Section body layout:
///   RelocSectionHeader
///   RelocKernelHeader[kernelCount]   // KernelTable order; indexes into entries
///   RelocEntry[totalEntryCount]      // all kernels' entries concatenated
///
/// Per-kernel slicing lives in this section so the 64-byte `KernelEntry`
/// remains independent of relocation storage.

struct RelocSectionHeader {
    uint32_t kernelCount;     // == KernelTable entry count
    uint32_t entrySize;       // == sizeof(RelocEntry); forward-compat self-check
    uint64_t totalEntryCount; // total RelocEntry count across all kernels
};
static_assert(sizeof(RelocSectionHeader) == 16, ".ccuobj RelocSectionHeader layout");

struct RelocKernelHeader {
    uint64_t entryOff;   // starting RelocEntry index (not a byte offset)
    uint64_t entryCount; // number of entries for this kernel
};
static_assert(sizeof(RelocKernelHeader) == 16, ".ccuobj RelocKernelHeader layout");

/// Which physical base/table a relocated field belongs to.
enum class RelocPool : uint8_t {
    Xn = 0,  // += physical xn pool base
    Gsa = 1, // += physical gsa pool base
    Cke = 2, // += physical cke pool base
    Ms = 3,  // += physical ms pool base
    // Launch-time Physical lookups. Resolved via the loader's
    // PhysLookups callbacks, not a pool-base add — channel/notify physical ids
    // are only known at launch (the adapter dlsym's hcomm's CcuUrmaChannel).
    Channel = 4,      // physChannelId[*field - 1] (1-based; 0 = event mode)
    NotifyCkeRmt = 5, // getRmtCke(*field) — notify_set SyncCKE.rmtCKEId
    NotifyCkeLoc = 6, // getLocCke(*field) — notify_wait ClearCKE.waitCKEId
    // hcomm loopback channel selector. The encoded field value is interpreted
    // by the runtime: 0 = self-loop channel, 1 = inter-die loop channel
    //.
    LoopChannel = 7,
    ExternalXn = 8,  // provider-owned external Xn binding
    ExternalCke = 9, // provider-owned external CKE binding
    // HComm channel-owned local Xn. channelArg is 1-based and reserved carries
    // the logical channel Xn index; the provider resolves localXnIds[index].
    ChannelXn = 10,
    // HComm channel-owned remote Xn used by SyncXn. channelArg is 1-based and
    // the field value is the logical remote-Xn index; the provider resolves
    // remoteXnIds[index].
    ChannelRmtXn = 11,
};

/// How to apply the relocation to the field at `byteOff`.
///
/// Kinds 0-2 relocate a 16-bit id field (applyResourceRelocs /
/// applyChannelNotifyRelocs). Kinds 3-5 fill a *64-bit immediate*
/// field of a LoadImd* instruction with a runtime-computed value
/// (applyImmediateRelocs); the existing 16-bit appliers skip them. These back
/// the LoadVariable/StoreVariable 7-instr macro, whose `varAddr` / token
/// immediates are only known at register time.
enum class RelocKind : uint8_t {
    AddBase = 0,    // *field += physBase[pool]            (unconditional)
    AddBaseTok = 1, // if (*field) *field += physBase[pool] (token sentinel 0)
    Physical = 2,   // *field = physTable[pool][*field]    (channel/notify)
    // - 64-bit immediate fills --
    VarBackingAddr = 3,   // imm holds logical varId; *imm64 = xnBaseAddr +
                          //   XN_PER_SIZE * (physXnBase + varId)
    CcuResSpaceToken = 4, // *imm64 = ccuResSpaceToken (per-die runtime const)
    MemToken = 5,         // *imm64 = memToken (per-kernel runtime const)
};

/// One relocation: the field at `byteOff` within CcuInstr `instrIdx`
/// (kernel-local) is relocated against `pool` using `kind`. All relocated
/// fields are 16-bit (every CcuInstr id field is uint16_t); the loader reads
/// `*reinterpret_cast<uint16_t*>(base + byteOff)`.
struct RelocEntry {
    uint32_t instrIdx; // which CcuInstr (kernel-local, 0-based)
    uint16_t byteOff;  // field's byte offset within the 32B CcuInstr
    uint8_t pool;      // RelocPool
    uint8_t kind;      // RelocKind
    // abiRev 4: notify CKE lookup must name the owning channel formal because
    // ClearCKE does not carry a channelId. 1-based, matching Channel fields;
    // 0 means this relocation has no channel selector (resource/loop/shared CKE).
    uint16_t channelArg;
    uint16_t reserved; // ChannelXn: channel-local Xn index; otherwise zero
};
static_assert(sizeof(RelocEntry) == 12, "RelocEntry packed to 12 bytes");

inline bool hasValidChannelSelector(const RelocEntry& r, uint32_t channelArgCount)
{
    const auto pool = static_cast<RelocPool>(r.pool);
    if (pool == RelocPool::ChannelXn || pool == RelocPool::ChannelRmtXn)
        return r.kind == static_cast<uint8_t>(RelocKind::Physical) && r.channelArg != 0 &&
               r.channelArg <= channelArgCount;
    if (r.reserved != 0)
        return false;
    if (pool != RelocPool::NotifyCkeRmt && pool != RelocPool::NotifyCkeLoc)
        return r.channelArg == 0;
    if (pool == RelocPool::NotifyCkeRmt && r.channelArg == 0)
        return false;
    return r.channelArg <= channelArgCount;
}

} // namespace ccu::ccuobj

#endif // CCU_WRITER_CCU_RELOC_FORMAT_H
