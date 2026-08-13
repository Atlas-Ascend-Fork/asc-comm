# Hcomm Shared-Jetty Batch Write Two-Device Sample

This sample validates two AIV `WriteNbi` WQEs assembled through two shared-Jetty channels and submitted to their
shared SQ by one `BatchCommit` and one SQ doorbell. `Drain` then waits for both CQEs.

The sample uses two ranks and two NPUs on one host. Each rank creates two logical channels to its only peer. This
validates multiple channel entities, UB-side WQE batching, a single commit, and CQ completion on a shared local
Jetty. `MakeMultiChannelHandle` takes an explicit `sharedQueueTag`: channels with the same endpoint and tag share a
Jetty, while a different tag selects a different shared Jetty. The sample does not validate multiple distinct peers;
that topology requires at least three ranks.

Prerequisites:

- Ascend 950PR or Ascend 950DT with at least two NPUs;
- an HCOMM package containing the shared-Jetty `HcclChannelAcquireWithConfig` control plane;
- the development run-package logic from `origin/run` merged with this Batch implementation and the resulting
  package installed into the active CANN;
- a sourced CANN environment with `ASCEND_HOME_PATH` pointing to that installation.

The Host helper is header-only. The sample consumes both its Host header and its AICore headers directly from the
active CANN installation and does not build an additional Host shared library.

Build and run:

```bash
cmake -S examples/hcomm_batch_write -B build/examples/hcomm_batch_write
cmake --build build/examples/hcomm_batch_write -j
./build/examples/hcomm_batch_write/hcomm_batch_write
```

The executable forks two local rank processes and uses a local socket pair for RootInfo, remote device-address
exchange, and synchronization. A successful run prints `shared-Jetty batch write passed` for both ranks and
`hcomm batch write test passed` from the parent process.
