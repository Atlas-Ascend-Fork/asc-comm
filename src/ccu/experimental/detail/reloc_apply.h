/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
// Step 4: table-driven `.ccuobj` microcode relocation.
// The writer emits a RELOCATION section that
// records, per id-typed CcuInstr field, which physical resource pool it belongs
// to and how to apply the base. A loader replays that table mechanically — no
// per-opcode knowledge — replacing the hand-written `relocateMicrocode` switch
// in lib/Runtime/ccu_aot_adapter.cpp (the B-plan that silently drifts when the
// writer's field layout changes).
// This header is deliberately hcomm-free: it addresses fields by raw byte
// offset into the 32-byte instruction array, so the adapter and the host-side
// equivalence test can use the same single source of truth without depending
// on an instruction representation type.
//===----------------------------------------------------------------------===//

#ifndef CCU_WRITER_RELOC_APPLY_H
#define CCU_WRITER_RELOC_APPLY_H

#include "ccu_reloc_format.h"

#include <cstddef>
#include <cstdint>

namespace ccu::ccuobj {

/// Physical pool base ids, indexed by RelocPool. hcomm allocates each resource
/// pool as a contiguous segment whose base is generally non-zero (observed on
/// hardware: xn@144, gsa@48, cke@224); these come from the runtime's resource
/// repository query at register time.
struct PhysBases {
    uint32_t xn = 0;
    uint32_t gsa = 0;
    uint32_t cke = 0;
    uint32_t ms = 0;
};

/// Apply one kernel's resource-pool relocations to its microcode buffer.
/// `instrBytes` points at the kernel's 32-byte instruction array.
/// Each RelocEntry names a 16-bit field by (instrIdx, byteOff); the field is
/// rebased against `base` per its pool and kind:
///   - AddBase:    *field += physBase[pool]                 (unconditional)
///   - AddBaseTok: if (*field) *field += physBase[pool]     (token sentinel 0)
///   - Physical:   skipped here — channel/notify ids are resolved at launch
///.
/// Channel / NotifyCkeRmt / NotifyCkeLoc pools are likewise skipped (they only
/// appear with Physical). This mirrors the writer's buildKernelRelocs
/// AddBase/AddBaseTok semantics field-for-field.
/// `instrCount` bounds the target field within the microcode buffer: an entry
/// whose `instrIdx >= instrCount` (or whose 2-byte field would not fit in its
/// 32-byte instruction) is skipped instead of writing out of bounds — a crafted
/// or corrupt .ccuobj must not turn relocation into an OOB heap write. The
/// default (SIZE_MAX) keeps the instrIdx bound off for callers that pass only a
/// known-good table; the byteOff bound always applies (valid tables never
/// exceed it, so behaviour is unchanged for well-formed input). In-repo callers
/// (loader + adapter) pass the real instrCount.
constexpr size_t kInstructionSize = 32;
constexpr size_t kMaxU16FieldOffset = 30;
constexpr size_t kMaxU64FieldOffset = 24;

inline void applyResourceRelocs(
    uint8_t* instrBytes, const RelocEntry* relocs, size_t n, const PhysBases& base, size_t instrCount = SIZE_MAX)
{
    for (size_t k = 0; k < n; ++k) {
        const RelocEntry& r = relocs[k];
        // 64-bit immediate kinds are applyImmediateRelocs' job.
        if (r.kind > static_cast<uint8_t>(RelocKind::Physical))
            continue;
        if (r.byteOff > kMaxU16FieldOffset || r.instrIdx >= instrCount)
            continue; // OOB guard
        uint32_t b = 0;
        switch (static_cast<RelocPool>(r.pool)) {
            case RelocPool::Xn:
                b = base.xn;
                break;
            case RelocPool::Gsa:
                b = base.gsa;
                break;
            case RelocPool::Cke:
                b = base.cke;
                break;
            case RelocPool::Ms:
                b = base.ms;
                break;
            default:
                continue; // Channel / NotifyCke -> Physical, not handled here
        }
        auto* f =
            reinterpret_cast<uint16_t*>(instrBytes + static_cast<size_t>(r.instrIdx) * kInstructionSize + r.byteOff);
        switch (static_cast<RelocKind>(r.kind)) {
            case RelocKind::AddBase:
                *f = static_cast<uint16_t>(*f + b);
                break;
            case RelocKind::AddBaseTok:
                if (*f)
                    *f = static_cast<uint16_t>(*f + b);
                break;
            case RelocKind::Physical:
                // launch-time channel/notify lookup — not a pool base add.
                break;
            case RelocKind::VarBackingAddr:
            case RelocKind::CcuResSpaceToken:
            case RelocKind::MemToken:
                // Filtered above and applied by applyImmediateRelocs.
                break;
        }
    }
}

/// Launch-time physical lookups for channel/notify relocation (
/// Step 2b). Unlike resource pools (a compile-time-known base add), channel ids
/// and notify CKE ids are only resolved at launch — the adapter dlsym's hcomm's
/// CcuUrmaChannel methods. Each callback maps a logical field value to its
/// physical value; on out-of-range / failure a callback must return its input
/// unchanged (no-op), matching the hand-written relocateChannelNotify.
/// Plain function pointers + ctx (not std::function) keep this header zero-
/// dependency and match the adapter's dlsym style.
struct PhysLookups {
    uint16_t (*channel)(void* ctx, uint16_t argIdx1); // 0 → return 0 (event mode)
    uint16_t (*channelRmtXn)(void* ctx, uint16_t channelArg,
                             uint16_t logIdx); // channel.getRmtXn
    uint16_t (*notifyRmtCke)(void* ctx, uint16_t channelArg,
                             uint16_t logIdx); // channel.getRmtCke
    uint16_t (*notifyLocCke)(void* ctx, uint16_t channelArg,
                             uint16_t logIdx);          // channel.getLocCke/shared CKE
    uint16_t (*loopChannel)(void* ctx, uint16_t value); // hcomm reserveChannalId[0]
    void* ctx;
};

struct ExternalLookups {
    uint16_t (*externalXn)(void* ctx, uint16_t slot);
    uint16_t (*externalCke)(void* ctx, uint16_t slot);
    void* ctx;
};

/// Apply one kernel's channel/notify (Physical) relocations to its microcode.
/// Only RelocKind::Physical entries on the Channel / NotifyCkeRmt / NotifyCkeLoc
/// pools are handled; resource-pool entries are skipped (applyResourceRelocs'
/// job). ChannelXn intentionally is not handled here: its value is a HComm
/// channel POD localXnIds[index] lookup, so the HComm provider applies it while
/// materializing the instruction image. Each field at (instrIdx, byteOff) is
/// replaced via the matching callback. This mirrors the adapter's
/// relocateChannelNotify field-for-field; the writer emits these entries from
/// buildKernelRelocs.
/// `instrCount` bounds the target field within the microcode buffer (same
/// contract as applyResourceRelocs): out-of-range entries are skipped, never
/// written out of bounds.
inline void applyChannelNotifyRelocs(
    uint8_t* instrBytes, const RelocEntry* relocs, size_t n, const PhysLookups& lk, size_t instrCount = SIZE_MAX)
{
    for (size_t k = 0; k < n; ++k) {
        const RelocEntry& r = relocs[k];
        if (static_cast<RelocKind>(r.kind) != RelocKind::Physical)
            continue;
        if (r.reserved != 0)
            continue;
        if (r.byteOff > kMaxU16FieldOffset || r.instrIdx >= instrCount)
            continue; // OOB guard (16-bit)
        auto* f =
            reinterpret_cast<uint16_t*>(instrBytes + static_cast<size_t>(r.instrIdx) * kInstructionSize + r.byteOff);
        switch (static_cast<RelocPool>(r.pool)) {
            case RelocPool::Channel:
                if (lk.channel)
                    *f = lk.channel(lk.ctx, *f);
                break;
            case RelocPool::ChannelRmtXn:
                if (lk.channelRmtXn)
                    *f = lk.channelRmtXn(lk.ctx, r.channelArg, *f);
                break;
            case RelocPool::NotifyCkeRmt:
                if (lk.notifyRmtCke)
                    *f = lk.notifyRmtCke(lk.ctx, r.channelArg, *f);
                break;
            case RelocPool::NotifyCkeLoc:
                if (lk.notifyLocCke)
                    *f = lk.notifyLocCke(lk.ctx, r.channelArg, *f);
                break;
            case RelocPool::LoopChannel:
                if (lk.loopChannel)
                    *f = lk.loopChannel(lk.ctx, *f);
                break;
            default:
                break; // Xn/Gsa/Cke/Ms -> applyResourceRelocs
        }
    }
}

inline void applyExternalRelocs(
    uint8_t* instrBytes, const RelocEntry* relocs, size_t n, const ExternalLookups& lk, size_t instrCount = SIZE_MAX)
{
    for (size_t k = 0; k < n; ++k) {
        const RelocEntry& r = relocs[k];
        if (static_cast<RelocKind>(r.kind) != RelocKind::Physical)
            continue;
        if (r.reserved != 0)
            continue;
        if (r.byteOff > kMaxU16FieldOffset || r.instrIdx >= instrCount)
            continue;
        auto* f =
            reinterpret_cast<uint16_t*>(instrBytes + static_cast<size_t>(r.instrIdx) * kInstructionSize + r.byteOff);
        switch (static_cast<RelocPool>(r.pool)) {
            case RelocPool::ExternalXn:
                if (lk.externalXn)
                    *f = lk.externalXn(lk.ctx, *f);
                break;
            case RelocPool::ExternalCke:
                if (lk.externalCke)
                    *f = lk.externalCke(lk.ctx, *f);
                break;
            default:
                break;
        }
    }
}

/// Register-time runtime values for the LoadVariable/StoreVariable
/// 64-bit immediate fills. The adapter computes these during register (it has
/// the per-die TransDep-equivalent info): `xnBaseAddr` from
/// CcuResSpecifications::GetXnBaseAddr, `physXnBase` = physical base of the Xn pool
/// (PhysBases.xn), `xnPerSize` = CCU_RESOURCE_XN_PER_SIZE, and the two tokens
/// from hcomm's token APIs. The host-side byte-level test passes zeros.
struct ImmValues {
    uint64_t xnBaseAddr = 0;
    uint64_t xnPerSize = 0;  // CCU_RESOURCE_XN_PER_SIZE
    uint32_t physXnBase = 0; // == PhysBases.xn
    uint64_t ccuResSpaceToken = 0;
    uint64_t memToken = 0;
};

/// Apply one kernel's 64-bit immediate relocations. Only the
/// VarBackingAddr / CcuResSpaceToken / MemToken kinds are handled; everything
/// else is skipped (resource/channel appliers' job). Each entry's `byteOff`
/// names the 8-byte `immediate` field of a LoadImd* instruction; the OOB guard
/// requires the full 8 bytes to fit (byteOff <= 24) so a corrupt table cannot
/// drive an out-of-bounds heap write. For VarBackingAddr the field initially
/// holds the *logical* var id (writer-stamped), rebased here to the physical
/// backing address.
inline void applyImmediateRelocs(
    uint8_t* instrBytes, const RelocEntry* relocs, size_t n, const ImmValues& iv, size_t instrCount = SIZE_MAX)
{
    for (size_t k = 0; k < n; ++k) {
        const RelocEntry& r = relocs[k];
        RelocKind kind = static_cast<RelocKind>(r.kind);
        if (kind != RelocKind::VarBackingAddr && kind != RelocKind::CcuResSpaceToken && kind != RelocKind::MemToken)
            continue;
        if (r.byteOff > kMaxU64FieldOffset || r.instrIdx >= instrCount)
            continue; // OOB (8 bytes)
        auto* f =
            reinterpret_cast<uint64_t*>(instrBytes + static_cast<size_t>(r.instrIdx) * kInstructionSize + r.byteOff);
        switch (kind) {
            case RelocKind::VarBackingAddr: {
                uint64_t logicalVarId = *f; // writer stamped the logical id here
                *f = iv.xnBaseAddr + iv.xnPerSize * (iv.physXnBase + logicalVarId);
                break;
            }
            case RelocKind::CcuResSpaceToken:
                *f = iv.ccuResSpaceToken;
                break;
            case RelocKind::MemToken:
                *f = iv.memToken;
                break;
            default:
                break;
        }
    }
}

} // namespace ccu::ccuobj

#endif // CCU_WRITER_RELOC_APPLY_H
