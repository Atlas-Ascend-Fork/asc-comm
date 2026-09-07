#pragma once

#if not defined(__CCE__) or defined(__CLION_IDE__)
#define __HOST_ONLY__
#endif

#ifdef __CLION_IDE__
#define __CCE_STUB_DISABLE_ADDRESS_SPACE_QUALIFIERS__
#define __CCE_STUB_USE_LOCAL_WRAPPER__
#define __CCE_AICORE_SUPPORT_SIMT__
#define __clang__
#include <cce_stubs/cce_stubs.h>
#endif

#if not defined(__CCE__)
#define __aicore__
#define __gm__
#define __ubuf__
#define __global__
#define __vector__
#define __mix__(a, b)
#define __simt_vf__
#define __simt_callee__
#define __simd_vf__
#define __simd_callee__
#define __forceinline__ inline
#endif
