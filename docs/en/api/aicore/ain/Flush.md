# Flush

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT: Supports the URMA protocol
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 training series products/Atlas A3 inference series products: Not supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 training series products/Atlas A2 inference series products: Not supported
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Not supported
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference series products AI Core: Not supported
- Atlas inference series products Vector Core: Not supported
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training series products: Not supported
<!-- end id6 -->
<!-- npu="x90" id7 -->
- Kirin X90: Not supported
<!-- end id7 -->

## Function Description

Wait for communication tasks on all peer channels in the specified communication team to complete.

`Flush` traverses all ranks in the team except the current rank, resolves the corresponding communication channel based on the `contextIndex` of the `Ain` instance, and invokes the underlying Hcomm completion-wait API on each channel.

## Function Prototype

```cpp
__aicore__ inline void Flush(AscendC::HcommTeamHandle team);
```

## Parameter Description

**Table 1**  API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Communication team handle. |

## Return Value

No return value.

## Constraints

- The Host side shall create the team before invocation.
- `team` must be a valid device-side handle.
- `Flush` waits for submitted tasks on all peer channels in the team to complete. To wait for a single peer channel, call `FlushAsync` to obtain the channel handle and then call `Wait`.
- Currently, only the `COMM_PROTOCOL_UBC_CTP` protocol path is supported.

## Example

The calling example of Flush is as follows:

```cpp
ain.Put(team, peer, dstWin, dstOffset, srcWin, srcOffset, bytes);
ain.Signal<AinSignalInc>(team, peer, AinSignalInc{signalWindow, signalOffset});
ain.Flush(team);    // Wait for communication tasks on all peer channels in the team to complete
```
