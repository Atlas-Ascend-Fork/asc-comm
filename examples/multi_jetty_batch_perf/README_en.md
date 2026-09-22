# Multi-Jetty SIMT validation

This example publishes multiple Jettys concurrently and uses one consumer to attribute shared-CQ completions to their JFS owners. It supports Write, Read and WriteWithNotify.

```bash
# Source your CANN environment first. Build exactly one batch specialization.
bash build.sh --batch-size 1
# Write/Read/Notify x J1/J2/J4/J8: twelve cases.
bash run.sh --batch-size 1
# Optional single case.
bash run.sh --batch-size 1 --api write --jetty-counts 4
# Repeat build/run for batches 8,32,64,128,256 to cover all 72 cases.
```

`--batch-size` selects both requests per batch and lanes per Jetty, with one request per lane. `--jetty-counts` selects the Jetty counts. Lane count cannot be configured independently. Fixed geometry: 64B, 1024 timed requests, 96 warmup requests rounded up to a batch, two ranks.

Each artifact contains one parallel VF. The build timeout is 5 minutes and the stack limit is 1152B; register usage is recorded per VF geometry. Layout=matrix-v3, batch size and binary SHA must match the completed build. Incremental builds reuse resource records only for the identical object. `--clean` removes only the selected build directory. See `--help` for supported options.

main.cpp owns host orchestration; host_setup.h owns rank/channel setup; simt_kernel.cpp owns the VF lifecycle; simt_submit.h owns publication; simt_completion.h owns completion windows/snapshots; validation.h owns complete data and completion checks. Each lane maintains its own SQ writes. Three block barriers coordinate preparation, publication and the next batch, so groups still wait at those boundaries. Single-consumer CQ ownership does not serialize Jetty publication.

J8/BS256 still uses two stream-ordered kernels of four Jettys, sharing completion state. Output reports this geometry. Read validates the initiator; Write/Notify validate the receiver; every warmup/timed payload and Notify flag is checked.

Each batch reports12 RESULT rows and one MATRIX_SUMMARY. Full per-rank logs remain alongside the summary. Validation failures are collected; timeout/signal/setup failures abort. Performance uses raw device ticks and cannot directly be compared with archived SIMD ns/GB/s until the clock units are aligned.

Host tests cover accounting, all72 geometries, script orchestration and full example syntax with SDK declarations substituted. Target CANN compilation and NPU behavior remain separately verified.
