/*
 * The ASC translation unit owns the DFX host implementation and the queue-reader
 * AIV kernel.  Consumers include only the declaration header and link the
 * resulting shared library.
 */
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#include "kernel_operator.h"
#include "hcomm/dfx/hcomm_channel_dump.h"
#include "host/hcomm_dfx_collect.h"
#include "host/hcomm_dfx_exception.h"
#include "host/hcomm_dfx_parse.h"

namespace AscendC {

int32_t HcommChannelExceptionCallbackRegister(const void *kernelSymbol)
{
    if (kernelSymbol == nullptr) {
        std::printf("[HcommDfxDump] bind exception callback failed: kernelSymbol is null\n");
        std::fflush(stdout);
        return HCOMM_FAILED;
    }

    aclrtFuncHandle funcHandle = nullptr;
    aclrtBinHandle binHandle = nullptr;
    if (aclrtGetFuncBySymbol(kernelSymbol, &funcHandle) != ACL_SUCCESS ||
        aclrtFunctionGetBinary(funcHandle, &binHandle) != ACL_SUCCESS ||
        aclrtBinarySetExceptionCallback(binHandle, HcommDfx::ExceptionCallback, nullptr) != ACL_SUCCESS) {
        std::printf("[HcommDfxDump] bind exception callback failed\n");
        std::fflush(stdout);
        return HCOMM_FAILED;
    }

    HcommDfx::MarkExceptionCallbackBound();
    HcommDfx::ClearPendingException();
    // Runs once per process at application request; print unconditionally so the callback binding stays visible by default.
    std::printf("[HcommDfxDump] exception callback registered\n");
    std::fflush(stdout);
    return HCOMM_SUCCESS;
}

uint32_t HcommChannelGetDumpInfoSize()
{
    return static_cast<uint32_t>(sizeof(HcommDfxChannelDumpData));
}

int32_t HcommChannelInfoDump(uint64_t channel, char *buf, uint32_t len)
{
    const ChannelHandle channelHandle = static_cast<ChannelHandle>(channel);
    const uint32_t dumpLen = static_cast<uint32_t>(sizeof(HcommDfxChannelDumpData));
    const bool callerBufUsable = (buf != nullptr && len >= dumpLen);

    bool shouldDump = !HcommDfx::IsExceptionCallbackBound() || HcommDfx::HasPendingException();
    if (!shouldDump) {
        // Successful no-op. Leave a deterministic snapshot: zero the caller buffer so it never carries stale
        // bytes from an earlier dump; a zeroed magic lets the caller detect "no dump occurred this call".
        if (callerBufUsable) {
            std::memset(buf, 0, dumpLen);
        }
        return HCOMM_SUCCESS;
    }

    // Collect into the caller buffer when it is usable; otherwise fall back to a local snapshot (print only).
    HcommDfxChannelDumpData local;
    void *work = callerBufUsable ? static_cast<void *>(buf) : static_cast<void *>(&local);
    int32_t ret = HcommDfx::DumpChannelInfo(1U, &channelHandle, work, dumpLen);
    if (ret == HCOMM_SUCCESS) {
        HcommDfx::ClearPendingException();
    }
    int32_t curDev = -1;
    (void)aclrtGetDevice(&curDev);
    HcommDfx::Trace("HcommChannelInfoDump ret=%d len=%u callerBuf=%d chan=0x%llx curDev=%d excDev=%u pending=%d", ret,
        dumpLen, callerBufUsable ? 1 : 0, static_cast<unsigned long long>(channel), curDev,
        HcommDfx::g_exception.deviceId.load(std::memory_order_relaxed),
        HcommDfx::HasPendingException() ? 1 : 0);
    return ret;
}

} // namespace AscendC
