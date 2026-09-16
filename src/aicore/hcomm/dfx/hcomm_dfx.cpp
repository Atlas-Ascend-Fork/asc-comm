/*
 * The ASC translation unit owns the DFX host implementation and the queue-reader
 * AIV kernel.  Consumers include only the declaration header and link the
 * resulting shared library.
 */
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include "kernel_operator.h"
#include "hcomm/dfx/hcomm_channel_dump.h"
#include "host/hcomm_dfx_collect.h"
#include "host/hcomm_dfx_exception.h"
#include "host/hcomm_dfx_parse.h"

namespace AscendC {
namespace {
constexpr const char *HCOMM_DFX_DUMP_LOCK_FILE = "/tmp/hcomm_dfx_dump.lock";

int32_t CopyDumpTextToBuffer(const std::string &text, char *buf, uint32_t len)
{
    if (buf == nullptr) {
        return HCOMM_SUCCESS;
    }
    if (len == 0U) {
        return text.empty() ? HCOMM_SUCCESS : HCOMM_FAILED;
    }
    const uint32_t copyLen = static_cast<uint32_t>(std::min<size_t>(text.size(), static_cast<size_t>(len - 1U)));
    if (copyLen != 0U) {
        std::memcpy(buf, text.data(), copyLen);
    }
    buf[copyLen] = '\0';
    return text.size() < static_cast<size_t>(len) ? HCOMM_SUCCESS : HCOMM_FAILED;
}

void LogDumpTextLocked(const std::string &text)
{
    int32_t fd = open(HCOMM_DFX_DUMP_LOCK_FILE, O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        HcommDfx::LogDumpText(text);
        return;
    }
    while (flock(fd, LOCK_EX) != 0) {
        if (errno == EINTR) {
            continue;
        }
        close(fd);
        HcommDfx::LogDumpText(text);
        return;
    }
    HcommDfx::LogDumpText(text);
    (void)flock(fd, LOCK_UN);
    close(fd);
}

constexpr uint32_t HCOMM_DFX_DUMP_DELAY_MS = 60U * 1000U;
constexpr uint32_t HCOMM_DFX_DUMP_WAIT_MS = 10U * 60U * 1000U;

struct DeferredDumpRequest {
    DeferredDumpRequest(uint64_t channelValue, aclrtContext contextValue, int32_t deviceValue)
        : channel(channelValue), context(contextValue), deviceId(deviceValue)
    {}

    uint64_t channel;
    aclrtContext context;
    int32_t deviceId;
    std::atomic<bool> consumed{false};
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
};

struct DeferredDumpState {
    std::mutex mutex;
    std::vector<std::shared_ptr<DeferredDumpRequest>> requests;
    bool atExitRegistered = false;
};

DeferredDumpState &GetDeferredDumpState()
{
    static DeferredDumpState state;
    return state;
}

int32_t DumpChannelInfoSync(uint64_t channel, char *buf, uint32_t len, bool checkPending)
{
    const ChannelHandle channelHandle = static_cast<ChannelHandle>(channel);

    if (checkPending && HcommDfx::HcommDfxException::IsBound() && !HcommDfx::HcommDfxException::HasPending()) {
        if (buf != nullptr && len != 0U) {
            buf[0] = '\0';
        }
        return HCOMM_SUCCESS;
    }

    std::string out;
    int32_t ret = HcommDfx::DumpChannelInfoToString(1U, &channelHandle, out);
    LogDumpTextLocked(out);
    int32_t copyRet = CopyDumpTextToBuffer(out, buf, len);
    if (ret == HCOMM_SUCCESS) {
        HcommDfx::HcommDfxException::ClearPending();
    }
    if (copyRet != HCOMM_SUCCESS) {
        HcommDfx::LogError("dump text buffer too small textLen=%zu bufLen=%u chan=0x%llx", out.size(), len,
            static_cast<unsigned long long>(channel));
    }
    return ret == HCOMM_SUCCESS ? copyRet : ret;
}

void MarkDeferredDumpDone(const std::shared_ptr<DeferredDumpRequest> &request)
{
    if (request == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(request->mutex);
        request->done = true;
    }
    request->cv.notify_all();
}

void WaitDeferredDumpDone(const std::shared_ptr<DeferredDumpRequest> &request)
{
    if (request == nullptr) {
        return;
    }
    std::unique_lock<std::mutex> lock(request->mutex);
    const bool completed = request->cv.wait_for(lock, std::chrono::milliseconds(HCOMM_DFX_DUMP_WAIT_MS),
        [&request] { return request->done; });
    if (!completed) {
        HcommDfx::LogError("deferred dump wait timeout chan=0x%llx request=%p waitMs=%u",
            static_cast<unsigned long long>(request->channel), request.get(), HCOMM_DFX_DUMP_WAIT_MS);
    }
}

void RunDeferredDumpRequest(const std::shared_ptr<DeferredDumpRequest> &request, const char *source)
{
    if (request == nullptr || request->consumed.exchange(true)) {
        return;
    }
    if (request->context != nullptr) {
        aclError setCtxRet = aclrtSetCurrentContext(request->context);
        if (setCtxRet != ACL_SUCCESS) {
            HcommDfx::LogError("deferred dump set context failed source=%s ret=%d ctx=%p chan=0x%llx", source,
                setCtxRet, request->context, static_cast<unsigned long long>(request->channel));
        }
    } else if (request->deviceId >= 0) {
        aclError setDevRet = aclrtSetDevice(request->deviceId);
        if (setDevRet != ACL_SUCCESS) {
            HcommDfx::LogError("deferred dump set device failed source=%s ret=%d device=%d chan=0x%llx", source,
                setDevRet, request->deviceId, static_cast<unsigned long long>(request->channel));
        }
    }
    int32_t ret = DumpChannelInfoSync(request->channel, nullptr, 0U, false);
    if (ret != HCOMM_SUCCESS) {
        HcommDfx::LogError("deferred dump failed source=%s ret=%d chan=0x%llx", source, ret,
            static_cast<unsigned long long>(request->channel));
    }
    MarkDeferredDumpDone(request);
}

void DeferredDumpWorker(std::shared_ptr<DeferredDumpRequest> request)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(HCOMM_DFX_DUMP_DELAY_MS));
    RunDeferredDumpRequest(request, "worker");
}

void FlushDeferredDumpsAtExit()
{
    std::vector<std::shared_ptr<DeferredDumpRequest>> requests;
    {
        std::lock_guard<std::mutex> lock(GetDeferredDumpState().mutex);
        requests = GetDeferredDumpState().requests;
    }
    for (const auto &request : requests) {
        RunDeferredDumpRequest(request, "atexit");
    }
}

int32_t QueueDeferredDump(uint64_t channel)
{
    int32_t deviceId = -1;
    (void)aclrtGetDevice(&deviceId);
    aclrtContext context = nullptr;
    (void)aclrtGetCurrentContext(&context);

    std::shared_ptr<DeferredDumpRequest> request;
    bool needStartWorker = false;

    {
        std::lock_guard<std::mutex> lock(GetDeferredDumpState().mutex);
        for (const auto &queued : GetDeferredDumpState().requests) {
            if (queued != nullptr && queued->channel == channel && queued->context == context &&
                queued->deviceId == deviceId) {
                request = queued;
                break;
            }
        }
        if (request == nullptr) {
            try {
                request = std::make_shared<DeferredDumpRequest>(channel, context, deviceId);
            } catch (const std::bad_alloc &) {
                HcommDfx::LogError("deferred dump request alloc failed chan=0x%llx",
                    static_cast<unsigned long long>(channel));
                return HCOMM_FAILED;
            }
            GetDeferredDumpState().requests.push_back(request);
            if (!GetDeferredDumpState().atExitRegistered) {
                GetDeferredDumpState().atExitRegistered = true;
                if (std::atexit(FlushDeferredDumpsAtExit) != 0) {
                    HcommDfx::LogError("deferred dump atexit register failed");
                }
            }
            needStartWorker = true;
        }
    }

    if (needStartWorker) {
        try {
            std::thread(DeferredDumpWorker, request).detach();
        } catch (const std::system_error &error) {
            HcommDfx::LogError("deferred dump worker create failed: %s", error.what());
            MarkDeferredDumpDone(request);
            return HCOMM_FAILED;
        }
    }
    WaitDeferredDumpDone(request);
    return HCOMM_SUCCESS;
}
} // namespace

int32_t HcommChannelExceptionCallbackRegister(const void *kernelSymbol)
{
    if (kernelSymbol == nullptr) {
        HcommDfx::LogError("bind exception callback failed: kernelSymbol is null");
        return HCOMM_FAILED;
    }

    aclrtFuncHandle funcHandle = nullptr;
    aclrtBinHandle binHandle = nullptr;
    if (aclrtGetFuncBySymbol(kernelSymbol, &funcHandle) != ACL_SUCCESS ||
        aclrtFunctionGetBinary(funcHandle, &binHandle) != ACL_SUCCESS ||
        aclrtBinarySetExceptionCallback(binHandle, HcommDfx::ExceptionCallback, nullptr) != ACL_SUCCESS) {
        HcommDfx::LogError("bind exception callback failed");
        return HCOMM_FAILED;
    }

    HcommDfx::MarkExceptionCallbackBound();
    HcommDfx::ClearPendingException();
    HcommDfx::LogInfo("exception callback registered");
    return HCOMM_SUCCESS;
}

int32_t HcommChannelInfoDump(uint64_t channel, char *buf, uint32_t len)
{
    // The print-only form is used by exception callbacks. Defer it to a host
    // worker so queue-reader launch never happens inside the callback context.
    if (buf == nullptr && len == 0U) {
        return QueueDeferredDump(channel);
    }
    return DumpChannelInfoSync(channel, buf, len, true);
}

} // namespace AscendC
