# Init

## Function Description
Initializes the Hcomm temporary workspace used by ordinary `ChannelHandle` interfaces. Ordinary interfaces on both the `COMM_PROTOCOL_ROCE` and `COMM_PROTOCOL_UBC_CTP` paths require this API before communication tasks are submitted.

The BatchHandle workflow does not depend on `Init`. `MakeBatchHandle` binds the batched WQE buffer, and batch `Drain` reuses that buffer as CQE scratch space.

## Function Prototype
```cpp
__aicore__ inline int32_t Init(__ubuf__ uint8_t* buff, uint32_t len);

template <typename T>
__aicore__ inline int32_t Init(const AscendC::LocalTensor<T>& buff, uint32_t len);
```

## Parameter Description
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
- This interface initializes workspace only for the ordinary `ChannelHandle` workflow. Batch `ReadNbi`, `WriteNbi`, `WriteWithNotifyNbi`, `BatchCommit`, and batch `Drain` do not require it.
