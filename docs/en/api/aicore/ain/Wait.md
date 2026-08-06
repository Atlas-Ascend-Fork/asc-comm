# Wait

## Function Description

Wait for communication tasks on a specified communication channel to complete.

Typically, call `FlushAsync` first to obtain the communication channel handle of a specified peer, and then call `Wait` to wait for submitted tasks on that channel to complete.

## Function Prototype

```cpp
__aicore__ inline void Wait(AscendC::ChannelHandle& channelHandle);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channelHandle` | Input | Communication channel handle, usually returned by `FlushAsync`. |

## Return Value

No return value.

## Usage Constraints

- `channelHandle` must be a valid communication channel handle.
- `Wait` blocks and waits for submitted tasks on this channel to complete through the underlying Hcomm completion-wait API.
- Currently, only the `COMM_PROTOCOL_UBC_CTP` protocol path is supported.
