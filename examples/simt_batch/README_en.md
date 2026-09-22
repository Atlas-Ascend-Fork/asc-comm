# Hcomm SIMT Batch minimal example

This example shows the smallest complete single-lane explicit-batch flow: create a `BatchHandle`, execute one
`WriteNbi → BatchCommit` pair, call `Drain`, and validate the remote data.

The essential Kernel-side flow is:

```cpp
constexpr uint32_t kWorkspaceBytes = AscendC::simt::HcommSimtBatchBufferBytes(1U);
alignas(128) __ubuf__ uint64_t workspace[kWorkspaceBytes / sizeof(uint64_t)];

AscendC::simt::Hcomm<> hcomm;
if (hcomm.Init(nullptr, 0U) != 0) {
    return;
}
auto batch = hcomm.MakeBatchHandle(
    channel, reinterpret_cast<__ubuf__ uint8_t*>(workspace), kWorkspaceBytes, remote, 1U);
if (hcomm.WriteNbi(batch, remote, local, 64U) != 0 || hcomm.BatchCommit(batch) != 0) {
    return;
}
if (hcomm.Drain(batch) != 0) {
    return;
}
```

The BatchHandle binds the remote registration containing `remote`; subsequent destination addresses must remain
in that registration. The final `1U` specifies that the Write WQE occupies one WQEBB in this example.

## Build and run

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/simt_batch
bash build.sh
bash run.sh                 # two devices
bash run.sh --devices 8     # eight devices paired as 0→1, 2→3, 4→5, and 6→7
```

The example creates its communicator with `HcclGetRootInfo` and `HcclCommInitRootInfo`; no rank table is required.
The script prints `Status=PASS` and returns zero only when every rank's calls and receiver-side data validation pass.
