/** Copyright (c) 2026 Huawei Technologies Co., Ltd. */
#ifndef ASCCOMM_CCU_UT_CCU_URMA_CHANNEL_COMPAT_H
#define ASCCOMM_CCU_UT_CCU_URMA_CHANNEL_COMPAT_H

#include <memory>

#include <hccl/hccl_types.h>

#include "hcomm_primitives.h"

namespace hccl { class DeviceMem; }

namespace hcomm {
using EndpointHandle = void *;
struct HcommChannelDesc {};
struct HcommMemHandle {};
struct CommMem {};
struct ChannelStatus {};
enum class HcommChannelKind { INVALID };

class CcuTransport { public: virtual ~CcuTransport(); };
class CcuConnection { public: virtual ~CcuConnection(); };

class Channel {
public:
    virtual ~Channel() = default;
    virtual HcclResult UpdateMemInfo(HcommMemHandle *memHandles, uint32_t memHandleNum);
    virtual HcommChannelKind GetChannelKind() const;
    virtual HcclResult Serialize(std::shared_ptr<hccl::DeviceMem> &out);
    virtual void AddPtrArrayDevMem(std::shared_ptr<hccl::DeviceMem> ptrArrayMem);
};

class CcuUrmaChannel : public Channel {
public:
    CcuUrmaChannel(EndpointHandle locEndpointHandle, const HcommChannelDesc &channelDesc);
    ~CcuUrmaChannel() override = default;
    HcclResult Init();
    ChannelStatus GetStatus();
    HcclResult GetNotifyNum(uint32_t *notifyNum) const;
    HcclResult GetRemoteMems(uint32_t *memNum, CommMem **remoteMem, char ***memInfos);
    HcclResult UpdateMemInfo(HcommMemHandle *memHandles, uint32_t memHandleNum) override;
    HcclResult Clean();
    HcclResult Resume();
    HcclResult ChannelFence();
    HcclResult NotifyRecord(uint32_t remoteNotifyIdx);
    HcclResult NotifyWait(uint32_t localNotifyIdx, uint32_t timeout);
    HcclResult WriteWithNotify(void *dst, const void *src, uint64_t len, uint32_t remoteNotifyIdx);
    HcclResult Write(void *dst, const void *src, uint64_t len);
    HcclResult Read(void *dst, const void *src, uint64_t len);
    uint32_t GetDieId() const;
    uint32_t GetChannelId() const;
    HcclResult GetRmtCkeByIndex(uint32_t index, uint32_t &rmtCkeId) const;
    HcclResult GetRmtXnByIndex(uint32_t index, uint32_t &rmtXnId) const;
    HcclResult GetLocCkeByIndex(uint32_t index, uint32_t &locCkeId) const;
    HcclResult GetLocXnByIndex(uint32_t index, uint32_t &locXnId) const;
    HcclResult GetRmtBuffer(uint64_t &addr, uint32_t &size, uint32_t &tokenId, uint32_t &tokenValue) const;
};
} // namespace hcomm

#endif
