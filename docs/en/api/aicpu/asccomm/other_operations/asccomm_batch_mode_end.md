# asccomm_batch_mode_end

<!-- md-trans-meta sourceCommit=e0aec142dc7ea9a47f1450a1f4a83ddab29157bc translatedAt=2026-11-04T10:07:03.577Z pushedAt=2026-11-09T06:47:54.987Z nl=2 -->

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

This API is used to end the batch submission mode started by [asccomm_batch_mode_start](asccomm_batch_mode_start.md). When this API is called, the communication tasks cached by the current AI CPU execution thread in this mode are submitted together. A successful return from this API indicates that the submission action succeeded, not that all hardware communication tasks have completed.

## Function Prototype

```cpp
int32_t asccomm_batch_mode_end(const char *batchTag)
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| batchTag | Input | Reserved parameter. The current implementation does not read or validate this parameter, and **NULL** can be passed. |

## Return Value

The return value is `int32_t`, with the following meanings.

| Return Value | Description |
| --- | --- |
| `ASCCOMM_SUCCESS`(0) | The batch submission mode is ended and the cached tasks are submitted successfully; this value is also returned when there are no tasks to submit. |
| `ASCCOMM_E_PTR` | The thread resource or local task cache pointer is null, or the address required when submitting cached tasks is null. |
| `ASCCOMM_E_PARA` | The thread resource, communication task submission queue, or context parameter is invalid. |
| `ASCCOMM_E_TIMEOUT` | When cached tasks are submitted, the communication queue space is insufficient and the wait times out. |
| `ASCCOMM_E_INTERNAL` | An internal exception occurs when submitting cached tasks. |

## Usage Constraints

- **Start** and **End** must be called in pairs, and **Start**, **End**, and the data-plane communication tasks submitted between them must be executed in the same AI CPU execution thread.
- **End** submits only the unsubmitted tasks cached by the current AI CPU execution thread in batch mode; the current cache capacity is 128, and tasks are submitted early when the cache is full.
- When there are no cached tasks to submit during the batch mode, **End** still returns `ASCCOMM_SUCCESS`.
- End first closes batch mode and then submits the cached tasks of the registered thread resources; after successful submission, it clears the list of registered thread resources. If submission fails, batch mode is already closed, but the list of registered thread resources is retained, so you can call **End** again directly to retry submitting the same batch of resources. Before the retry succeeds, newly submitted data plane tasks will not enter batch caching again.
- A successful return from **End** indicates that the cached task submission action succeeded, not that all hardware communication tasks have been completed.

## Call Example

```cpp
// batchTag is a reserved parameter and is not read by the current implementation. nullptr is passed in this example.
const char *batchTag = nullptr;
// Enable batch mode.
int32_t ret = asccomm_batch_mode_start(batchTag);

// During batch mode, communication tasks are cached first.
// ...

// End batch mode and submit the cached tasks together.
ret = asccomm_batch_mode_end(batchTag);
(void)ret; // Error handling is omitted in this example. Check the return value in actual calls.
```
