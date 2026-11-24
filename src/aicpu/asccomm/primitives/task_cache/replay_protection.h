/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#pragma once
#include "binary_template.h"
#include "../../resource/connection/udma_data_struct.h"
#include "hcomm_res_entity_defs.h"
namespace AsccommBinaryCache {
struct ReplayProtection {
    static constexpr uint32_t NOTIFY_VALUE = 1U;
    struct Protection {
        const ChannelEntity* channel;
        uint64_t address, length;
        bool local;
        uint32_t token, value;
        uint64_t generation = 0;
    };
    enum class ProtectionKind { Local, Remote, DrainLocal, DrainRemote, Notify };
    struct ProtectionRef {
        ProtectionKind kind;
        uint64_t address, length;
        uint32_t index;
        uint32_t registration = UINT32_MAX;
        AddressPlan addressPlan;
        uint32_t staticSlot = UINT32_MAX;
    };
    struct StaticProtection {
        const ChannelEntity* channel;
        ProtectionRef ref;
        uint32_t token = 0, value = 0;
        uint64_t address = 0;
    };
    std::vector<StaticProtection> staticProtection;
    struct Registration {
        const ChannelEntity* channel;
        const HcommRegedBufferEntity* captured;
        bool local;
        uint32_t role;
        uint64_t begin, end, generation = 0;
        uint32_t token = 0, value = 0;
        bool uniform = false, valid = false;
        AddressPlan addressPlan;
    };
    std::vector<Registration> registrations;
    struct UbPatch {
        ProtectionRef remote{}, local{}, notify{};
        uint32_t localOffset = 0;
        bool hasLocal = false, hasNotify = false, valid = false;
    };
    std::vector<Protection> protection;
    uint64_t generation = 0;
    Entry* liveEntry = nullptr;
    bool is_range_contained(uint64_t base, uint64_t capacity, uint64_t addr, uint64_t size)
    {
        return addr >= base && size <= capacity && (addr - base) <= (capacity - size);
    }

    const HcommRegedBufferEntity* find_buffer(
        const HcommRegedBufferEntity* buffers, uint32_t bufferNum, uint64_t addr, uint64_t size,
        bool useFirstAsFallback)
    {
        if (buffers == nullptr || bufferNum == 0) {
            return nullptr;
        }
        for (uint32_t index = 0; index < bufferNum; ++index) {
            if (buffers[index].type == HCOMM_REGED_BUFFER_RMA &&
                is_range_contained(
                    buffers[index].bufferInfo.rma.addr, buffers[index].bufferInfo.rma.size, addr, size)) {
                return &buffers[index];
            }
        }
        if (useFirstAsFallback && buffers[0].type == HCOMM_REGED_BUFFER_RMA) {
            return &buffers[0];
        }
        return nullptr;
    }

    bool NotifyProtection(
        const ChannelEntity& channel, uint32_t index, uint64_t address, uint64_t size, uint32_t& token, uint32_t& value)
    {
        if (!channel.remoteNotifyAddr || index >= channel.remoteNotifyNum) {
            return Reject(AsccommRejectReason::RemoteNotifyRange);
        }
        const auto& notify = channel.remoteNotifyAddr[index];
        const auto& region = notify.notifyInfo.rmaRt;
        if (notify.type == HCOMM_REGED_NOTIFY_RMA_RT) {
            NotifyIndex() = index;
        }
        if (notify.type != HCOMM_REGED_NOTIFY_RMA_RT || region.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA ||
            !is_range_contained(region.addr, region.size, address, size)) {
            return Reject(AsccommRejectReason::RemoteNotifyRange);
        }
        token = region.protectionInfo.memInfo.ub.tokenId;
        value = region.protectionInfo.memInfo.ub.tokenValue;
        return true;
    }
    bool ub_protection(
        const ChannelEntity& c, uint64_t addr, uint64_t len, bool local, uint32_t& token, uint32_t& value)
    {
        ReplayProtection::Protection* cacheSlot = nullptr;
        if (!protection.empty()) {
            const size_t mask = protection.size() - 1;
            size_t index = ((reinterpret_cast<uintptr_t>(&c) >> 4) ^ (addr >> 4) ^ (len * 17) ^ local) & mask;
            for (size_t attempts = 0; attempts <= mask; ++attempts, index = (index + 1) & mask) {
                auto& cached = protection[index];
                if (cached.generation != generation) {
                    cacheSlot = &cached;
                    break;
                }
                if (cached.channel == &c && cached.address == addr && cached.length == len && cached.local == local) {
                    token = cached.token;
                    value = cached.value;
                    return true;
                }
            }
        }
        const auto* buffers = local ? c.localBufferAddr : c.remoteBufferAddr;
        const auto count = local ? c.localBufferNum : c.remoteBufferNum;
        // Match build_ub_slices: local addresses may use the first registered protection.
        // Remote ranges remain strict and skip the reserved slot. Resolve live tokens on
        // every replay; no cached token and no per-hit fallback warning.
        const auto* b = find_buffer(
            !local && buffers && count ? buffers + 1 : buffers, !local && count ? count - 1 : count, addr, len, local);
        if (b && b->bufferInfo.rma.protectionInfo.type == HCOMM_PROTECTION_TYPE_URMA) {
            token = b->bufferInfo.rma.protectionInfo.memInfo.ub.tokenId;
            value = b->bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
            if (cacheSlot) {
                *cacheSlot = {&c, addr, len, local, token, value, generation};
            }
            return true;
        }
        return false;
    }
    bool rdma_protection(const ChannelEntity& c, uint64_t addr, uint64_t len, bool local, uint32_t& key)
    {
        const auto* remote = c.remoteBufferAddr && c.remoteBufferNum ? c.remoteBufferAddr + 1 : nullptr;
        const auto* b = find_buffer(
            local ? c.localBufferAddr : remote,
            local ? c.localBufferNum : (c.remoteBufferNum ? c.remoteBufferNum - 1 : 0), addr, len, false);
        if (b && b->bufferInfo.rma.protectionInfo.type == HCOMM_PROTECTION_TYPE_RDMA) {
            const auto& p = b->bufferInfo.rma.protectionInfo.memInfo.roce;
            key = local ? p.lkey : p.rkey;
            return true;
        }
        return false;
    }
    uint64_t address(uint32_t low, uint32_t high) { return low | (uint64_t(high) << 32); }
    bool patch_ub(const ChannelEntity& channel, const Event& event, const Patch& field, uint8_t* data)
    {
        static_assert(sizeof(Asc::UdmaSqeWrite) == 64, "UB WQE ABI");
        static_assert(sizeof(Asc::UdmaSqeWriteWithNotify) == 96, "UB notify WQE ABI");
        if ((event.length != 64 && event.length != 128) || field.field != 2 || field.offset != 0 ||
            (field.source != 0 && field.source != 2)) {
            return Reject(AsccommRejectReason::UbPatchEncoding);
        }
        alignas(8) uint8_t scratch[128]{};
        (void)memcpy_sp(scratch, sizeof(scratch), data, event.length);
        auto& common = *reinterpret_cast<Asc::UdmaSqeCommon*>(scratch);
        const bool notify = event.length == 128;
        if ((notify && common.opcode != 5) ||
            (!notify && common.opcode != Asc::UDMA_OPC_READ && common.opcode != Asc::UDMA_OPC_WRITE)) {
            return Reject(AsccommRejectReason::UbOpcode);
        }
        auto& normal = *reinterpret_cast<Asc::UdmaSqeWrite*>(scratch);
        auto& withNotify = *reinterpret_cast<Asc::UdmaSqeWriteWithNotify*>(scratch);
        auto& sge = notify ? withNotify.localU.sge : normal.u.sge;
        const uint64_t len = common.inlineEn ? common.inlineMsgLen : sge.length;
        uint32_t token = 0, value = 0;
        const auto remoteAddr = address(common.rmtAddr.rmtAddrLow, common.rmtAddr.rmtAddrHigh);
        auto notifyProtection = [&](uint64_t addr, uint64_t size) {
            if (field.span > UINT32_MAX) {
                return Reject(AsccommRejectReason::NotifyIndex);
            }
            return NotifyProtection(channel, static_cast<uint32_t>(field.span), addr, size, token, value);
        };
        if (field.source == 2) {
            if (!channel.remoteBufferAddr || !channel.remoteBufferNum) {
                return Reject(AsccommRejectReason::RemoteBufferMissing);
            }
            const auto& b = channel.remoteBufferAddr[0];
            const auto& r = b.bufferInfo.rma;
            if (b.type != HCOMM_REGED_BUFFER_RMA || r.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA ||
                !is_range_contained(r.addr, r.size, remoteAddr, len)) {
                return Reject(AsccommRejectReason::RemoteBufferRange);
            }
            token = r.protectionInfo.memInfo.ub.tokenId;
            value = r.protectionInfo.memInfo.ub.tokenValue;
        } else if (common.inlineEn) {
            if (!notifyProtection(remoteAddr, len)) {
                return Reject(AsccommRejectReason::InlineProtection);
            }
        } else if (!ub_protection(channel, remoteAddr, len, false, token, value)) {
            return Reject(AsccommRejectReason::RemoteProtection);
        }
        common.rmtObjId = token;
        common.rmtTokenValue = value;
        if (common.inlineEn) {
            // All supported native inline calls are the four-byte NOTIFY_VALUE.
            // Arbitrary inline payloads require an explicit dynamic source and are rejected.
            uint32_t payload = 0;
            (void)memcpy_sp(&payload, sizeof(payload), normal.u.inlineData.data, sizeof(payload));
            if (notify || len != sizeof(payload) || payload != NOTIFY_VALUE) {
                return Reject(AsccommRejectReason::InlinePayload);
            }
        } else {
            const auto localAddr = address(sge.dataAddrLow, sge.dataAddrHigh);
            if (field.source == 2) {
                if (!channel.localNotifyAddr || !channel.localNotifyNum) {
                    return Reject(AsccommRejectReason::LocalNotifyMissing);
                }
                const auto& b = channel.localNotifyAddr[0];
                const auto& r = b.notifyInfo.rmaRt;
                if (b.type != HCOMM_REGED_NOTIFY_RMA_RT || r.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA ||
                    !is_range_contained(r.addr, r.size, localAddr, len)) {
                    return Reject(AsccommRejectReason::LocalNotifyRange);
                }
                token = r.protectionInfo.memInfo.ub.tokenId;
            } else if (!ub_protection(channel, localAddr, len, true, token, value)) {
                return Reject(AsccommRejectReason::LocalProtection);
            }
            sge.tokenId = token;
        }
        if (notify) {
            auto& n = withNotify.notify;
            if (!notifyProtection(address(n.notifyAddrLow, n.notifyAddrHigh), sizeof(uint32_t))) {
                return Reject(AsccommRejectReason::WriteNotifyProtection);
            }
            n.notifyTokenId = token;
            n.notifyTokenValue = value;
        }
        (void)memcpy_sp(data, event.length, scratch, event.length);
        return true;
    }
    bool CompileUbPatch(
        const ChannelEntity& channel, const Event& e, const Patch& field, const uint8_t* data,
        ReplayProtection::UbPatch& plan)
    {
        plan = {}; // Reset active patches, including optional fields from older captures.
        alignas(8) uint8_t scratch[128]{};
        if ((e.length != 64 && e.length != 128) || e.offset % alignof(uint32_t)) {
            return false;
        }
        (void)memcpy_sp(scratch, sizeof(scratch), data, e.length);
        if (!patch_ub(channel, e, field, scratch)) {
            return false;
        }
        const auto& common = *reinterpret_cast<const Asc::UdmaSqeCommon*>(scratch);
        const bool notify = e.length == 128;
        const auto& normal = *reinterpret_cast<const Asc::UdmaSqeWrite*>(scratch);
        const auto& extended = *reinterpret_cast<const Asc::UdmaSqeWriteWithNotify*>(scratch);
        const auto& sge = notify ? extended.localU.sge : normal.u.sge;
        const uint64_t len = common.inlineEn ? common.inlineMsgLen : sge.length;
        using K = ReplayProtection::ProtectionKind;
        plan.remote = {
            field.source == 2 ? K::DrainRemote : (common.inlineEn ? K::Notify : K::Remote),
            address(common.rmtAddr.rmtAddrLow, common.rmtAddr.rmtAddrHigh),
            len,
            static_cast<uint32_t>(field.span),
            UINT32_MAX,
            {}};
        plan.hasLocal = !common.inlineEn;
        if (plan.hasLocal) {
            plan.local = {field.source == 2 ? K::DrainLocal : K::Local,
                          address(sge.dataAddrLow, sge.dataAddrHigh),
                          len,
                          0,
                          UINT32_MAX,
                          {}};
            plan.localOffset = notify ? offsetof(Asc::UdmaSqeWriteWithNotify, localU) : offsetof(Asc::UdmaSqeWrite, u);
        }
        plan.hasNotify = notify;
        if (notify) {
            plan.notify = {K::Notify,        address(extended.notify.notifyAddrLow, extended.notify.notifyAddrHigh),
                           sizeof(uint32_t), static_cast<uint32_t>(field.span),
                           UINT32_MAX,       {}};
        }
        plan.valid = true;
        return true;
    }
    void BindRegistration(Entry& entry, const ChannelEntity& channel, ReplayProtection::ProtectionRef& ref)
    {
        using K = ReplayProtection::ProtectionKind;
        if (ref.kind != K::Local && ref.kind != K::Remote) {
            return;
        }
        const bool local = ref.kind == K::Local;
        const auto* buffers = local ? channel.localBufferAddr : channel.remoteBufferAddr;
        auto count = local ? channel.localBufferNum : channel.remoteBufferNum;
        if (!local && buffers && count) {
            ++buffers;
            --count;
        }
        const auto* selected = find_buffer(buffers, count, ref.address, ref.length, local);
        if (!selected || ref.length > UINT64_MAX - ref.address) {
            return;
        }
        uint32_t role = 2;
        if (entry.relocatable) {
            for (uint32_t i = 0; i < 2; ++i) {
                if (is_range_contained(
                        entry.captureRanges[i * 2], entry.captureRanges[i * 2 + 1], ref.address, ref.length)) {
                    role = i;
                }
            }
        }
        uint32_t index = 0;
        for (; index < registrations.size(); ++index) {
            const auto& g = registrations[index];
            if (g.channel == &channel && g.captured == selected && g.local == local && g.role == role) {
                break;
            }
        }
        if (index == registrations.size()) {
            registrations.push_back(
                {&channel, selected, local, role, ref.address, ref.address + ref.length, 0, 0, 0, false, false, {}});
        }
        auto& group = registrations[index];
        group.begin = std::min(group.begin, ref.address);
        group.end = std::max(group.end, ref.address + ref.length);
        ref.registration = index;
    }
    bool RegisteredProtection(
        const ChannelEntity& channel, const ReplayProtection::ProtectionRef& ref, uint64_t address, uint32_t& token,
        uint32_t& value)
    {
        const bool local = ref.kind == ReplayProtection::ProtectionKind::Local;
        if (ref.registration >= registrations.size()) {
            return ub_protection(channel, address, ref.length, local, token, value);
        }
        auto& group = registrations[ref.registration];
        if (group.generation != generation) {
            group.generation = generation;
            group.valid = group.uniform = false;
            uint64_t begin = group.begin;
            const uint64_t length = group.end - group.begin;
            if (liveEntry && !group.addressPlan.Resolve(
                                 liveEntry->captureRanges, liveEntry->replayRanges, liveEntry->relocatable, begin)) {
                return false;
            }
            const auto* buffers = local ? channel.localBufferAddr : channel.remoteBufferAddr;
            auto count = local ? channel.localBufferNum : channel.remoteBufferNum;
            if (!local && buffers && count) {
                ++buffers;
                --count;
            }
            const auto* selected = find_buffer(buffers, count, begin, length, local);
            if (selected) {
                const auto& region = selected->bufferInfo.rma;
                group.uniform = true;
                const bool contained = is_range_contained(region.addr, region.size, begin, length);
                // Preserve first-match and local-fallback rules for overlapping registrations.
                // Ambiguous groups must re-expand the original primitive: native
                // chooses protection for the whole operation, not individual WQEs.
                for (uint32_t i = 0; i < count; ++i) {
                    if (contained && &buffers[i] == selected) {
                        break;
                    }
                    const auto& candidate = buffers[i];
                    const auto& r = candidate.bufferInfo.rma;
                    if (candidate.type == HCOMM_REGED_BUFFER_RMA && r.size && r.size <= UINT64_MAX - r.addr &&
                        begin < r.addr + r.size && r.addr < begin + length) {
                        group.uniform = false;
                    }
                }
                group.valid = region.protectionInfo.type == HCOMM_PROTECTION_TYPE_URMA;
                if (group.valid) {
                    group.token = region.protectionInfo.memInfo.ub.tokenId;
                    group.value = region.protectionInfo.memInfo.ub.tokenValue;
                }
            }
        }
        if (!group.uniform) {
            return false; // preflight fallback before any publication
        }
        if (!group.valid) {
            return false;
        }
        token = group.token;
        value = group.value;
        return true;
    }
    bool ResolveCompiledProtection(
        const ChannelEntity& channel, const ReplayProtection::ProtectionRef& ref, uint32_t& token, uint32_t& value,
        uint64_t& mapped)
    {
        using K = ReplayProtection::ProtectionKind;
        mapped = ref.address;
        if (liveEntry && liveEntry->direct && ref.staticSlot != UINT32_MAX) {
            const auto& slot = staticProtection[ref.staticSlot];
            token = slot.token;
            value = slot.value;
            mapped = slot.address;
            return true;
        }
        if (liveEntry && liveEntry->direct && ref.registration != UINT32_MAX) {
            const auto& group = registrations[ref.registration];
            // The registration's union range was validated once before this loop.
            mapped = ref.addressPlan.role < 4 ? liveEntry->replayRanges[ref.addressPlan.role] + ref.addressPlan.offset :
                                                ref.address;
            token = group.token;
            value = group.value;
            return true;
        }
        if (liveEntry && !ref.addressPlan.Resolve(
                             liveEntry->captureRanges, liveEntry->replayRanges, liveEntry->relocatable, mapped)) {
            return Reject(AsccommRejectReason::RemoteProtection);
        }
        // Notify/drain storage belongs to the resource, never to a relocated user buffer.
        if (ref.kind != K::Local && ref.kind != K::Remote && mapped != ref.address) {
            return Reject(AsccommRejectReason::RemoteProtection);
        }
        if (liveEntry && liveEntry->direct && ref.registration != UINT32_MAX) {
            const auto& group = registrations[ref.registration];
            token = group.token;
            value = group.value;
            return group.valid && group.uniform;
        }
        auto current = ref;
        current.address = mapped;
        const auto& resolved = current;
        switch (ref.kind) {
            case K::Local:
                return RegisteredProtection(channel, ref, resolved.address, token, value) ||
                       Reject(AsccommRejectReason::LocalProtection);
            case K::Remote:
                return RegisteredProtection(channel, ref, resolved.address, token, value) ||
                       Reject(AsccommRejectReason::RemoteProtection);
            case K::Notify: {
                return NotifyProtection(channel, ref.index, resolved.address, resolved.length, token, value);
            }
            case K::DrainLocal: {
                if (!channel.localNotifyAddr || !channel.localNotifyNum) {
                    return Reject(AsccommRejectReason::LocalNotifyMissing);
                }
                const auto& b = channel.localNotifyAddr[0];
                const auto& r = b.notifyInfo.rmaRt;
                if (b.type != HCOMM_REGED_NOTIFY_RMA_RT || r.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA ||
                    !is_range_contained(r.addr, r.size, resolved.address, resolved.length)) {
                    return Reject(AsccommRejectReason::LocalNotifyRange);
                }
                token = r.protectionInfo.memInfo.ub.tokenId;
                value = r.protectionInfo.memInfo.ub.tokenValue;
                return true;
            }
            case K::DrainRemote: {
                if (!channel.remoteBufferAddr || !channel.remoteBufferNum) {
                    return Reject(AsccommRejectReason::RemoteBufferMissing);
                }
                const auto& b = channel.remoteBufferAddr[0];
                const auto& r = b.bufferInfo.rma;
                if (b.type != HCOMM_REGED_BUFFER_RMA || r.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA ||
                    !is_range_contained(r.addr, r.size, resolved.address, resolved.length)) {
                    return Reject(AsccommRejectReason::RemoteBufferRange);
                }
                token = r.protectionInfo.memInfo.ub.tokenId;
                value = r.protectionInfo.memInfo.ub.tokenValue;
                return true;
            }
        }
        return false;
    }
    bool RefreshUbPatch(const ChannelEntity& channel, const ReplayProtection::UbPatch& plan, uint8_t* data)
    {
        if (!plan.valid) {
            return Reject(AsccommRejectReason::UbPatchEncoding);
        }
        uint32_t token = 0, value = 0;
        uint64_t mapped = 0;
        if (!ResolveCompiledProtection(channel, plan.remote, token, value, mapped)) {
            return false;
        }
        auto& common = *reinterpret_cast<Asc::UdmaSqeCommon*>(data);
        common.rmtObjId = token;
        common.rmtTokenValue = value;
        if (liveEntry) {
            common.rmtAddr.rmtAddrLow = mapped;
            common.rmtAddr.rmtAddrHigh = mapped >> 32;
        }
        if (plan.hasLocal) {
            if (!ResolveCompiledProtection(channel, plan.local, token, value, mapped)) {
                return false;
            }
            auto& local = *reinterpret_cast<Asc::UdmaNormalSge*>(data + plan.localOffset);
            local.tokenId = token;
            if (liveEntry) {
                local.dataAddrLow = mapped;
                local.dataAddrHigh = mapped >> 32;
            }
        }
        if (plan.hasNotify) {
            if (!ResolveCompiledProtection(channel, plan.notify, token, value, mapped)) {
                return false;
            }
            auto& notify = reinterpret_cast<Asc::UdmaSqeWriteWithNotify*>(data)->notify;
            notify.notifyTokenId = token;
            notify.notifyTokenValue = value;
        }
        return true;
    }
};
} // namespace AsccommBinaryCache
