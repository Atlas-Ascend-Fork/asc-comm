# Init

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT: Supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 training series products/Atlas A3 inference series products: Supports the RoCE protocol but not the URMA protocol
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 training series products/Atlas A2 inference series products: Supports the RoCE protocol but not the URMA protocol
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Supports the RoCE protocol but not the URMA protocol
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference series products AI Core: Supports the RoCE protocol but not the URMA protocol
- Atlas inference series products Vector Core: Supports the RoCE protocol but not the URMA protocol
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training series products: Supports the RoCE protocol but not the URMA protocol
<!-- end id6 -->
<!-- npu="x90" id7 -->
- Kirin X90: Supports the RoCE protocol but not the URMA protocol
<!-- end id7 -->

## Function Description

Initializes the Hcomm temporary workspace used by ordinary `ChannelHandle` interfaces. Ordinary interfaces on both the `COMM_PROTOCOL_ROCE` and `COMM_PROTOCOL_UBC_CTP` paths require this interface to be called before communication tasks are submitted.

The BatchHandle workflow does not depend on `Init`. The batch WQE buffer is bound by `MakeBatchHandle`, and batch `Drain` reuses that buffer as CQE temporary space.

## Function Prototype

```cpp
__aicore__ inline int32_t Init(__ubuf__ uint8_t* buff, uint32_t len);

template <typename T>
__aicore__ inline int32_t Init(const AscendC::LocalTensor<T>& buff, uint32_t len);
```

## Parameter Description

**Table 1**  Template parameters

| Parameter | Description |
| --- | --- |
| `T` | Element type of the `LocalTensor`. |

**Table 2**  API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `buff` | Input | UB buffer or `LocalTensor` provided by the caller. |
| `len` | Input | Buffer length in bytes. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | Initialization succeeded. |
| `-1` | Initialization failed. |

## Constraints

- The RoCE path uses `buff` as the WQE/CQE/doorbell temporary workspace. The current minimum workspace size is 512 bytes.
- The UBC_CTP/URMA path uses `buff` as the WQE/CQE temporary workspace. The current minimum workspace size is 512 bytes.
- When initialized with `__ubuf__ uint8_t*`, the implementation uses the 32-byte aligned address of `buff` as the start address of the temporary workspace.
- When initialized with `LocalTensor`, the implementation directly uses the passed tensor. The RoCE path requires both `len` and `buff.GetSize()` to be no less than 512 bytes. The UBC_CTP/URMA path requires `len` to be no less than 512 bytes and not greater than `buff.GetSize()`.
- This interface initializes the workspace only for the ordinary `ChannelHandle` workflow. When the BatchHandle interfaces are used, this interface does not need to be called for batch `ReadNbi`, `WriteNbi`, `WriteWithNotifyNbi`, `BatchCommit`, or batch `Drain`.

## Example

The calling example of Init is as follows:

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
pipe->InitBuffer(hcommBuf, HCOMM_WORKSPACE_SIZE);
auto hcommTensor = hcommBuf.Get<uint8_t>();
hcomm.Init(hcommTensor, HCOMM_WORKSPACE_SIZE);
```
