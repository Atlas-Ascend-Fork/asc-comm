# asccomm_local_copy_on_thread

<!-- md-trans-meta sourceCommit=e0aec142dc7ea9a47f1450a1f4a83ddab29157bc translatedAt=2026-11-04T10:05:19.655Z pushedAt=2026-11-09T06:47:54.984Z nl=2 -->

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

Header file path: `"aicpu/hcomm/asccomm_primitives.h"`.

Provides local memory copy. It copies the memory data of length **len** pointed to by **src** to the memory of the same length pointed to by **dst**.

## Function Prototype

```cpp
int32_t asccomm_local_copy_on_thread(ThreadHandle thread, void *dst, const void *src, uint64_t len)
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| thread | Input | Communication thread handle. After the control plane creates the thread resource, this handle is passed in as an AI CPU kernel parameter; it must not be 0 when called. The type is `ThreadHandle`, which can be used after including `"aicpu/hcomm/asccomm_primitives.h"`. |
| dst | Output | Destination memory address, which is device memory. |
| src | Input | Source memory address, which is device memory. |
| len | Input | Data length (in bytes). |

## Return Value

The return value is `int32_t`, with the following meanings.

| Return Value | Description |
| --- | --- |
| `ASCCOMM_SUCCESS` (0) | The task is generated (no task is generated when `len` is 0), which does not mean that the copy is complete. |
| `ASCCOMM_E_PTR` | The thread handle is 0, the source or destination address is null, or the thread internal task buffer address is null. |
| `ASCCOMM_E_PARA` | The thread resource description or the communication task submission queue parameter is invalid. |
| `ASCCOMM_E_TIMEOUT` | The communication task submission queue has insufficient space, and the wait for available space timed out. |
| `ASCCOMM_E_INTERNAL` | An internal operation failed, such as obtaining the task ID, querying or configuring the communication task submission queue, or submitting the copy task. |

## Usage Constraints

- **dst** and **src** must be non-NULL valid device addresses that can be directly accessed by the AI CPU.
- No local copy task is generated when `len` is **0**.

## Call Example

```cpp
// thread is a valid ThreadHandle resource handle passed in from the control plane; src and dst are device addresses that can be directly accessed by the AI CPU.
uint64_t len = 1024;
int32_t ret = asccomm_local_copy_on_thread(thread, dst, src, len);
```
