/** Copyright (c) 2026 Huawei Technologies Co., Ltd. */
#ifndef ASCCOMM_CCU_STRING_UTIL_H
#define ASCCOMM_CCU_STRING_UTIL_H

#include <cstdio>
#include <string>
#include <vector>

namespace Hccl {
template <typename... Args> std::string StringFormat(const char *format, Args... args)
{
    const int length = std::snprintf(nullptr, 0, format, args...);
    if (length < 0) return {};
    std::vector<char> buffer(static_cast<size_t>(length) + 1);
    (void)std::snprintf(buffer.data(), buffer.size(), format, args...);
    return {buffer.data(), static_cast<size_t>(length)};
}
} // namespace Hccl

#endif
