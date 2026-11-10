# local_addr

<!-- md-trans-meta sourceCommit=295a77ce184be22d92363353fa1bfa2ae30f20bd translatedAt=2026-11-05T01:35:49.445Z pushedAt=2026-11-09T06:49:22.780Z nl=2 -->

## Supported Products

<!-- npu="950" id1 -->
- Ascend 950PR&950DT products: Supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 products: Not supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 products: Not supported
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Not supported
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference products: Not supported
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training products: Not supported
<!-- end id6 -->

## Function Description

Header file: `#include "ccu/hcomm/ccu_primitives.hpp"`

`ccu::local_addr` is a C++ wrapper class for the local on-chip memory address within a CCU kernel. It is a composite object of "address (`address`) + token (`variable`)".

- Allocation upon construction: The default constructor applies for two virtual handles, `address` and `variable`, at once, which are used for `addr_` and `token` respectively.
-No release upon destruction: Hardware resources are not released upon destruction. Physical resources are managed uniformly over the lifecycle of the CCU instance, and virtual handles become invalid after translation.

> [!NOTE] Note
> Resource allocation follows a two-phase "virtual first, physical later" model: the `local_addr()` constructor only produces virtual handles, while the actual physical resources are allocated uniformly during the compilation phase.

CCU hardware does not accept process virtual addresses. Accessing on-chip memory requires a token converted by `HcommCcuGetMemToken`. The `addr_` field of `local_addr` stores the physical address or the tokenized VA (Virtual Address), and the `token` field stores the matching security token value.

## Class Declaration

```cpp
namespace AscendC {
namespace ccu {
class local_addr final {
public:
    local_addr();                         // Default constructor. Applies for two virtual handles, address and variable, at once.
    address addr_;                       // Local on-chip memory address field (address register).
    variable token;                      // Security token field (general-purpose register).
    ccu_local_addr_handle handle{0};       // Composite handle.
};
} // namespace ccu
} // namespace AscendC
```

## Constructor Description

- Default constructor

    Use `local_addr la;` to request two virtual handles: `address` and `variable`.

## Parameters

The parameters of the `local_addr` class are described as follows:

| Parameter | Type | Description |
| --- | --- | --- |
| `addr_` | `address` | Local on-chip memory address. Assign a value through `la.addr_ = imm` (immediate value) or `la.addr_ = var` (variable). For operator semantics, see [address](address.md). |
| `token` | `variable` | Security token value. After the host obtains it by calling `HcommCcuGetMemToken`, pass it into the kernel through `kernelArg` or `taskArgs`+`load_arg`, and then assign it to `la.token`. For operator semantics, see [variable](variable.md). |

## Usage Constraints

> [!CAUTION] Caution
> `token` is security information. It must not be printed in host or device logs, and must not be passed across ranks in plaintext.

- It can only be constructed during the compilation phase; constructing outside the compilation phase throws an exception carrying `CCU_E_PTR`.
- The default constructor only requests virtual handles and always succeeds without throwing an exception during the compilation phase; when `address`/`variable` physical resources are insufficient, the compilation phase returns `CCU_E_UNAVAIL` instead of throwing an exception at construction time.
- The copy/move constructors only copy the three fields `handle`, `addr_.handle`, and `token.handle`, and do not request new `address`/`variable`; after `local_addr l2 = l1;`, the two objects point to the same set of registers. In contrast, `operator=(const local_addr&)` emits two device-side register assignment instructions (moving the values of `other.addr_` and `other.token` over), so the two have asymmetric semantics.
- Do not save or compare the `handle` value outside the kernel; the handle becomes invalid after translation completes.
- Do not separately call `address()`/`variable()` on the embedded `addr_`/`token` fields to construct new objects — `local_addr()` has already completed the allocation of all subfields at once.
- The `addr_` field must be assigned a physical address accessible to the CCU or a tokenized VA. Passing a process virtual address that has not been tokenized directly will trigger a driver error.
- The `token` usually comes from the `HcommCcuGetMemToken` call on the host side, and must be paired with `addr_`. It must not be mixed with the remote token.

## Call Example

```cpp
using namespace AscendC::ccu;

// User-defined kernel argument struct. The host fills it and passes it through kernelArg.
// (src_addr is the local on-chip memory address, and src_token is obtained by the host through the HcommCcuGetMemToken call.)
struct my_kernel_arg {
    uint64_t src_addr;   // Local on-chip memory address.
    uint64_t src_token;  // Matching security token.
};

CcuResult my_kernel(ccu_kernel_arg arg) {  // arg is void*, pointing to my_kernel_arg passed by the host.
    auto* params = static_cast<my_kernel_arg*>(arg);

    local_addr src;
    // Method 1: Assign from the kernelArg at compile time (fixed as an immediate value).
    src.addr_ = params->src_addr;
    src.token = params->src_token;

    // Method 2: Inject through taskArgs at the dispatch phase (more flexible).
    variable addrVar, tokenVar;
    load_arg(addrVar, 0);    // taskArgs[0] is the address.
    load_arg(tokenVar, 1);   // taskArgs[1] is the token.
    local_addr src2;
    src2.addr_ = addrVar;    // Dynamic address in the dispatch phase.
    src2.token = tokenVar;

    return CCU_SUCCESS;
}
```
