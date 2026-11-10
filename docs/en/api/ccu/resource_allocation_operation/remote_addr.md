# remote_addr

<!-- md-trans-meta sourceCommit=295a77ce184be22d92363353fa1bfa2ae30f20bd translatedAt=2026-11-05T01:37:46.061Z pushedAt=2026-11-09T06:49:22.783Z nl=2 -->

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

`ccu::remote_addr` is a C++ wrapper class for the remote on-chip memory address within a CCU kernel. It is a composite object of "address (`address`) + token (`variable`)", and its structure is the same as that of [local_addr](local_addr.md).

- Allocation upon construction: the default constructor applies for two virtual handles at once, `address` and `variable` (used for `addr_` and `token`, respectively).
-No release upon destruction: Hardware resources are not released upon destruction. Physical resources are managed uniformly over the CCU instance lifecycle, and the virtual handles become invalid after translation.

> [!NOTE] Note
> Resource allocation follows a "virtual-first, physical-later" two-phase model: `remote_addr()` construction only produces virtual handles, and the actual physical resources are allocated uniformly at the compilation phase.

`remote_addr` is dedicated to cross-rank writes (`write`/`write_reduce`) and points to the target memory of the remote rank.

## Class Declaration

```cpp
namespace AscendC {
namespace ccu {
class remote_addr final {
public:
    remote_addr();                       // Default constructor. Applies for one address and one variable virtual handle at once.
    address addr_;                       // Remote on-chip memory address field (address register).
    variable token;                      // Remote security token field (general-purpose register).
    ccu_remote_addr_handle handle{0};    // Composite handle.
};
} // namespace ccu
} // namespace AscendC
```

## Constructor Description

- Default constructor

    Use `remote_addr ra;` to apply for one `address` virtual handle and one `variable` virtual handle.

## Parameters

| Parameter | Type | Description |
| --- | --- | --- |
| `addr_` | `address` | Remote on-chip memory target address. It must be filled with the remote rank's VA (Virtual Address) and token (the result of the remote `HcommCcuGetMemToken`). For operator semantics, see [address](address.md). |
| `token` | `variable` | Remote security token value. It must be paired with `addr_`, come from the remote rank, and must not be mixed with the local token. For operator semantics, see [variable](variable.md). |

## Usage Constraints

> [!CAUTION] Caution
> `token` is security information. It must not be printed in host or device logs, and must not be passed across ranks in plaintext. The `addr_` and `token` fields must be the values of the remote rank's target memory, and their source is completely different from that of the local `local_addr` field.

- It can only be constructed during the compilation phase. If constructed outside the compilation phase, an exception carrying `CCU_E_PTR` is thrown.
- The default constructor only applies for virtual handles and always succeeds without throwing an exception during the compilation phase. When `address`/`variable` physical resources are insufficient, the compilation phase returns `CCU_E_UNAVAIL` instead of throwing an exception at construction time.
- The copy/move constructors only copy the three fields `handle`, `addr_.handle`, and `token.handle`, and do not apply for new `address`/`variable` resources. After `remote_addr r2 = r1;`, the two objects point to the same set of registers. `operator=(const remote_addr&)` executes a device-side register assignment instruction (with the same semantics as `local_addr`), so the two are asymmetric.
- Do not save or compare the `handle` value outside the kernel. The handle becomes invalid after translation is complete.
- Do not call `address()`/`variable()` separately on the embedded `addr_`/`token` fields to construct new objects — `remote_addr()` has already allocated all subfields at once.
- `remote_addr` must be used together with [`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/en/api_ref/comm_opdev/datatype_definition/ChannelHandle.md) to perform cross-rank RDMA operations over an established channel. If it is used without a channel, the hardware exhibits undefined behavior.

## Call Example

```cpp
using namespace AscendC::ccu;

// Obtain the address and token of the remote rank from the peer through inter-process communication (such as a message-passing framework),
// and then pass them into the kernel through kernelArg or taskArgs+load_arg.
struct my_kernel_arg {
    ChannelHandle channel_handle;
    uint64_t remote_addr;
    uint64_t remote_token;
    uint64_t local_addr;
    uint64_t local_token;
};

CcuResult my_kernel(ccu_kernel_arg arg) {
    auto* params = static_cast<my_kernel_arg*>(arg);  // ccu_kernel_arg is void*. Cast it to the user input parameter struct first.

    remote_addr remote;
    // Assign from kernelArg at the compilation phase (fixed as an immediate value)
    remote.addr_ = params->remote_addr;
    remote.token = params->remote_token;

    // Use with the cross-rank write/write_reduce APIs
    ChannelHandle ch = params->channel_handle;
    local_addr local;
    local.addr_ = params->local_addr;
    local.token = params->local_token;
    variable len;
    event evt;
    len = 1024;

    write(ch, remote, local, len, evt);   // Write the local on-chip memory data to the peer on-chip memory
    event_wait(evt);

    return CCU_SUCCESS;
}
```
