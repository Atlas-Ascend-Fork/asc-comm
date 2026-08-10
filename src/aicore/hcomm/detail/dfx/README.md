# HCOMM DFX Channel Dump

This directory provides the private queue-read helper kernel used by the host-side HCOMM channel dump APIs.

## Files

- `include/aicore/hcomm/dfx/hcomm_channel_dump_info.h`

  Defines the stable dump data structures returned to host callers. The buffer contains channel metadata,
  local/remote buffer metadata, SQ/CQ context metadata, and sampled WQE/CQE raw bytes.

- `include/aicore/hcomm/dfx/hcomm_channel_dump.h`

  Provides the host APIs:

  ```cpp
  int32_t HcommDfxGetChannelDumpInfoSize(uint32_t channelNum, const ChannelHandle *channels, uint32_t *len);
  int32_t HcommDfxDumpChannelInfo(uint32_t channelNum, const ChannelHandle *channels, void *buffer, uint32_t len);
  int32_t HcommDfxParseAndPrintChannelInfo(const void *buffer, uint32_t len);
  ```

  The API reads ordinary channel metadata with device-to-host copy, then reads `sqVa`/`scqVa` queue entries
  through the local DFX queue-read kernel.

- `hcomm_dfx_queue_read_kernel.h`

  Defines the minimal AIV helper kernel used by `HcommDfxDumpChannelInfo` to read WQE/CQE raw bytes from queue VA
  that may not be safe to access with ordinary host-side device-to-host copy.

## Usage Pattern

The recommended flow is to keep the runtime exception callback lightweight. The callback records exception
metadata and the related `ChannelHandle`. After `aclrtSynchronizeStream` returns, call:

```cpp
uint32_t len = 0;
AscendC::HcommDfxGetChannelDumpInfoSize(1, &channel, &len);
std::vector<uint8_t> buffer(len);
AscendC::HcommDfxDumpChannelInfo(1, &channel, buffer.data(), len);
AscendC::HcommDfxParseAndPrintChannelInfo(buffer.data(), len);
```

The caller can also parse `AscendC::HcommDfxChannelDumpData` from `buffer.data()` when custom logging is required.

## Queue-Read Kernel Packaging

`HcommDfxDumpChannelInfo` loads `hcomm_dfx_queue_read_kernel_910_95.o` from the caller binary directory by default.
Set `HCOMM_DFX_QUEUE_READ_KERNEL_PATH` to override the object path. Set `HCOMM_DFX_DUMP_TRACE=1` only when tracing
the dump process itself.
