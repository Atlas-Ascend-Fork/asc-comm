# wait

## Function Description

wait for communication tasks on a specified communication channel to complete.

Typically, call `flush_async` first to obtain the communication channel handle of a specified peer, and then call `wait` to wait for submitted tasks on that channel to complete.

## Function Prototype

```cpp
__aicore__ inline void wait(AscendC::channel_handle& channel_handle);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel_handle` | Input | Communication channel handle, usually returned by `flush_async`. |

## Return Value

No return value.

## Usage Constraints

- `channel_handle` must be a valid communication channel handle.
- `wait` blocks and waits for submitted tasks on this channel to complete through the underlying asccomm completion-wait API.
- Currently, only the `comm_protocol_ubc_ctp` protocol path is supported.
