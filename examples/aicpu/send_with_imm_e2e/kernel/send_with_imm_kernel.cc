#include "asccomm_primitives.h"

#include <cstdint>

namespace {

struct KernelArgs {
    ThreadHandle thread{0};
    ChannelHandle channel{0};
    uintptr_t src{0};
    uint64_t len{0};
    uint32_t immData{0};
    int32_t ret{-1};
};

} // namespace

extern "C" void AsccommSendWithImmKernel(KernelArgs* args)
{
    if (args == nullptr) return;
    args->ret = asccomm_send_with_imm_on_thread(
        args->thread, args->channel, reinterpret_cast<const void*>(args->src), args->len, args->immData);
}
