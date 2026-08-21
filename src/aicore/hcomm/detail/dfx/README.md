# HCOMM DFX Channel Dump

This directory provides the host-side HCOMM channel dump implementation and the private queue-read helper kernel it
relies on.

## Public surface

Only two APIs are exported to operator developers, declared in `include/aicore/hcomm/dfx/hcomm_channel_dump.h`:

```cpp
int32_t HcommChannelExceptionCallbackRegister(const void *kernelSymbol);
int32_t HcommChannelInfoDump(ChannelHandle channel);
```

Everything else (dump data layout, size/collect helpers, pending-exception state) is internal to this directory and
is intentionally not part of the public header.

## Files

- `include/aicore/hcomm/dfx/hcomm_channel_dump.h`

  The public header. Declares only the two APIs above.

- `hcomm_channel_dump_info.h`

  Internal dump data layout (channel metadata, local/remote buffer metadata, SQ/CQ context metadata, and sampled
  WQE/CQE raw bytes). Raw-first: the collector stores raw struct bytes plus a small envelope, and decoding happens at
  print time. This header is internal — callers never see or parse these structs.

- `hcomm_channel_dump.cpp`

  Implements the two public APIs on top of internal helpers (`GetChannelDumpInfoSize` / `DumpChannelInfo` /
  `HasPendingException` / `ClearPendingException`, all in `namespace AscendC::HcommDfx`). It reads ordinary channel
  metadata with device-to-host copy, reads `sqVa`/`scqVa` queue entries through the local DFX queue-read kernel,
  stores the dump data in a scratch buffer, and prints the parsed channel information.

- `hcomm_dfx_queue_read_kernel.h`

  The minimal AIV helper kernel used by `DumpChannelInfo` to read WQE/CQE raw bytes from queue VA that may not be
  safe to access with ordinary host-side device-to-host copy.

- `hcomm_dfx_queue_reader.cpp`

  The host-side queue-read launch wrapper. This source should be compiled into the HCOMM target so the dump
  implementation can invoke the helper kernel without loading a standalone object file.

## Usage Pattern

Register a lightweight exception callback on the target kernel before launch. The callback only records that an
exception happened (no device-to-host copy, no printing, no launch — none of which are safe inside the exception
state). After `aclrtSynchronizeStream` returns an error, call the dump API with the related `ChannelHandle` at the
host safe point:

```cpp
AscendC::HcommChannelExceptionCallbackRegister(reinterpret_cast<const void *>(kernel_symbol));
kernel_symbol<<<1, nullptr, stream>>>(devContext);
aclrtSynchronizeStream(stream);                       // returns error when the kernel faulted
AscendC::HcommChannelInfoDump(channel);               // no-op if no exception was recorded
```

`HcommChannelInfoDump` internally checks the pending-exception flag first, so it is safe to call unconditionally: it
returns success without printing when no exception is pending. A pending exception is **consumed on the first
successful call** — the flag is cleared as soon as the dump is taken, so one fault is dumped exactly once and the
process can proceed to the next independent test round without re-printing stale state. The flag is also reset by
`HcommChannelExceptionCallbackRegister`, so re-registering before each round likewise starts from a clean slate.

> If an operator holds several channels and wants to dump each of them for a single fault, dump them **before** the
> flag is consumed — e.g. within one `DumpChannelInfo` call that takes a channel array — rather than relying on
> repeated `HcommChannelInfoDump` calls, since only the first call sees the pending flag.

## Queue-Read Kernel Packaging

`HcommChannelInfoDump` calls the queue-read launch wrapper linked into the HCOMM target. The helper kernel is
compiled into that target with ASC `<<<>>>` launch support, so callers do not need to provide any standalone
queue-read object file. Set `HCOMM_DFX_DUMP_TRACE=1` only when tracing the dump process itself.
