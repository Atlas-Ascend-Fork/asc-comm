# asccomm_batch_mode_start

<!-- md-trans-meta sourceCommit=e0aec142dc7ea9a47f1450a1f4a83ddab29157bc translatedAt=2026-11-04T10:07:43.376Z pushedAt=2026-11-09T06:47:54.987Z nl=2 -->

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

This API is used to enable the batch submission mode of the current AI CPU execution thread. Outside batch mode, data-plane communication tasks are submitted automatically. During batch mode, data-plane communication tasks to be delivered are first cached and are submitted together when [asccomm_batch_mode_end](asccomm_batch_mode_end.md) is called. A non-empty `batchTag` also isolates Thread resource sets of different batches in the current execution thread; an empty tag indicates a temporary batch.

## Function Prototype

```cpp
int32_t asccomm_batch_mode_start(const char *batchTag)
```

## Parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| batchTag | Input | Batch identifier. **NULL** or an empty string indicates a temporary batch; a non-empty string isolates and retrieves the Thread resource set of that batch. |

## Return Value

The return value is `int32_t`, with the following meanings.

| Return Value | Description |
| --- | --- |
| `ASCCOMM_SUCCESS`(0) | Batch submission mode enabled successfully. |

## Usage Constraints

- **Start** and **End** must be called in pairs, and **Start**, **End**, and the data-plane communication tasks submitted between them must be executed in the same AI CPU execution thread.
- **Start** records the current batch tag. **End** must pass the same tag to submit and clear that batch.
- The batch state applies only to the current AI CPU execution thread; **End** submits only the tasks cached in batch mode by the current execution thread.
- Data-plane communication tasks are automatically submitted by default. **Start** switches the execution thread to batch caching, and **End** submits the cached tasks and restores automatic submission.
- In the current implementation, tasks are submitted early when the internal buffer is full; the current cache capacity is 128.
- A successful return from this API indicates that batch mode has been set successfully, not that the data-plane tasks have been submitted or completed.

## Call Example

```cpp
const char *batchTag = "batch-1";
// Enable batch mode.
int32_t ret = asccomm_batch_mode_start(batchTag);

// During batch mode, communication tasks are cached first.
// ...

// Disable batch mode and submit the cached tasks together.
ret = asccomm_batch_mode_end(batchTag);
(void)ret; // Error handling is omitted in this example; check the return value in actual calls.
```
