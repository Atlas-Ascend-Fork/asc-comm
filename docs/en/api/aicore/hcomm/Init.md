# Init

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT: Supported
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

## Function Description

Initializes the Hcomm temporary workspace used by ordinary `ChannelHandle` interfaces. Ordinary interfaces on both the `COMM_PROTOCOL_ROCE` and `COMM_PROTOCOL_UBC_CTP` paths require this API before communication tasks are submitted.

The BatchHandle workflow does not depend on `Init`. Its workspace is provided through `MakeBatchHandle`.

## Function Prototype

```cpp
__aicore__ inline int32_t Init(__ubuf__ uint8_t* buff, uint32_t len);

template <typename T>
__aicore__ inline int32_t Init(const AscendC::LocalTensor<T>& buff, uint32_t len);
```

## Parameter Description

**Table 1**  Template Parameter Description

| Parameter | Description |
| --- | --- |
| `T` | Element type of `LocalTensor`. This parameter applies only to the `LocalTensor` overload. |

**Table 2**  Interface Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `buff` | Input | User-provided UB buffer or `LocalTensor`. |
| `len` | Input | Buffer length in bytes. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | Initialization succeeded. |
| `-1` | Initialization failed. |

## Constraints

- For the RoCE path, `buff` serves as the temporary workspace for WQE/CQE/doorbell. The minimum workspace size is currently 512 bytes.
- For the UBC_CTP/URMA path, `buff` serves as the temporary workspace for WQE/CQE. The minimum workspace size is currently 512 bytes.
- When initialized with `__ubuf__ uint8_t*`, the implementation takes the 32-byte aligned address of `buff` as the start address of the temporary workspace.
- When initialized with `LocalTensor`, the implementation directly uses the input tensor. For the RoCE path, both `len` and `buff.GetSize()` must be no less than 512 bytes. For the UBC_CTP/URMA path, `len` must be no less than 512 bytes and must not exceed `buff.GetSize()`.
- This interface is used only by the ordinary `ChannelHandle` workflow. BatchHandle interfaces do not require it.

## Example

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::LocalTensor<uint8_t> tmpBuffer; // Allocated from UB memory with at least 512 bytes
uint32_t tmpLen = 512U;

int32_t ret = hcomm.Init(tmpBuffer, tmpLen);
```
