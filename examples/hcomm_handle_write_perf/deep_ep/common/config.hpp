#pragma once

#include <cstdint>

namespace deep_ep {

using sf_pack_t = int16_t;

static constexpr int kNumMaxRanks = 256;
static constexpr int kNumMaxExperts = 2048;
static constexpr int kNumUbAlignmentBytes = 32;
static constexpr int kNumVectorRegisterBytes = 256;
static constexpr int64_t kNumAlignmentBytes = 2097152;

// TODO: make it as a template
static constexpr int kNumUbufWorkspaceBytes = 64 * 1024;

}  // namespace deep_ep
