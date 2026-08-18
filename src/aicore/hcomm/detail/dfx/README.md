# HCOMM DFX Channel Dump

This directory provides the private queue-read helper kernel used by the host-side HCOMM channel dump APIs.

## Files

- `include/aicore/hcomm/dfx/hcomm_channel_dump_info.h`

  Defines the stable dump data structures returned to host callers. The buffer contains channel metadata,
  local/remote buffer metadata, SQ/CQ context metadata, and sampled WQE/CQE raw bytes.

- `include/aicore/hcomm/dfx/hcomm_channel_dump.h`

  Declares the public host APIs:

	  ```cpp
	  int32_t HcommDfxGetChannelDumpInfoSize(uint32_t channelNum, const ChannelHandle *channels, uint32_t *len);
	  int32_t HcommDfxDumpChannelInfo(uint32_t channelNum, const ChannelHandle *channels, void *buffer, uint32_t len);
	  int32_t HcommDfxBindKernelException(const void *kernelSymbol, void *deviceContext);
	  bool HcommDfxHasPendingException();
	  void HcommDfxClearPendingException();
	  int32_t HcommDfxDumpAndPrintChannelIfException(ChannelHandle channel);
	  ```

- `hcomm_channel_dump.cpp`

  Implements the public dump APIs. The implementation reads ordinary channel metadata with device-to-host copy,
  then reads `sqVa`/`scqVa` queue entries through the local DFX queue-read kernel, stores the dump data in the
  caller buffer, and prints the parsed channel information.

- `hcomm_dfx_queue_read_kernel.h`

  Defines the minimal AIV helper kernel used by `HcommDfxDumpChannelInfo` to read WQE/CQE raw bytes from queue VA
  that may not be safe to access with ordinary host-side device-to-host copy.

- `hcomm_dfx_queue_reader.cpp`

  Provides the host-side queue-read launch wrapper. This source should be compiled into the HCOMM target so
  `HcommDfxDumpChannelInfo` can invoke the helper kernel without loading a standalone object file.

## Usage Pattern

The recommended flow is to let the DFX helper bind a lightweight exception callback to the target kernel. The
callback only records that an exception happened. After `aclrtSynchronizeStream` returns, call the convenience dump
API with the related `ChannelHandle`:

```cpp
AscendC::HcommDfxBindKernelException(reinterpret_cast<const void *>(kernel_symbol), devContext);
kernel_symbol<<<1, nullptr, stream>>>(devContext);
aclrtSynchronizeStream(stream);
AscendC::HcommDfxDumpAndPrintChannelIfException(channel);
```

If the caller needs to manage the dump buffer or parse `AscendC::HcommDfxChannelDumpData` manually, use the lower
level APIs directly:

```cpp
uint32_t len = 0;
AscendC::HcommDfxGetChannelDumpInfoSize(1, &channel, &len);
std::vector<uint8_t> buffer(len);
AscendC::HcommDfxDumpChannelInfo(1, &channel, buffer.data(), len);
```

The caller can also parse `AscendC::HcommDfxChannelDumpData` from `buffer.data()` when custom logging is required.

## Queue-Read Kernel Packaging

`HcommDfxDumpChannelInfo` calls the queue-read launch wrapper linked into the HCOMM target. The helper kernel is
compiled into that target with ASC `<<<>>>` launch support, so callers do not need to provide any standalone
queue-read object file. Set `HCOMM_DFX_DUMP_TRACE=1` only when tracing the dump process itself.
