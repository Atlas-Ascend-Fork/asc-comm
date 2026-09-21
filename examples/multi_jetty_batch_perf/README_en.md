# Multi-Jetty SIMT validation

The root example contains only the current parallel publication and CompletionV1 path. Historical SIMD, serial kernels, scope experiments and profiling live in [diagnostics/](diagnostics/README_en.md); they are excluded from the normal build.

```bash
# Source your CANN environment first. Build exactly one batch specialization.
bash build.sh --batch-size 1
# Write/Read/Notify x J1/J2/J4/J8: twelve cases.
bash run.sh --batch-size 1
# Optional single case.
bash run.sh --batch-size 1 --api write --jetty-counts 4
# Repeat build/run for batches 8,32,64,128,256 to cover all 72 cases.
```

For an explicit `-O0` diagnostic build, use the same option on both scripts:

```bash
bash build.sh --batch-size 64 -O0 --clean
bash run.sh --batch-size 64 -O0
```

This uses `build/simt-matrix-bs64-o0`; the default build is unchanged. The first scripted build uses `--clean` to generate its resource and SHA records, including after a direct CMake build. Subsequent builds may omit `--clean`. Results include `Optimization=O0` and must not be used as the optimized performance baseline.


Each Jetty uses batch-size lanes. Fixed geometry:64B,1024 timed requests,96 warmup requests rounded up to a batch,two ranks. No control or trace environment settings are required.

Each artifact contains one parallel VF. Build timeout is5min; stack stays<=1152B. Register usage is recorded per VF geometry rather than capped at the old1024-lane case's32 registers. Binary identity and SHA must match the completed build. --clean only removes the selected profile. Previous --matrix-batch and fixed matrix runner options remain supported.

main.cpp owns host orchestration; host_setup.h owns rank/channel setup; simt_kernel.cpp owns the VF lifecycle; simt_submit.h owns publication; simt_completion.h owns completion windows/snapshots; validation.h owns complete data and completion checks. Public comm code, publication barriers and lane-owned SQ maintenance are unchanged.

J8/BS256 still uses two stream-ordered kernels of four Jettys, sharing completion state. Output reports this geometry. Read validates the initiator; Write/Notify validate the receiver; every warmup/timed payload and Notify flag is checked.

Each batch reports12 RESULT rows and one MATRIX_SUMMARY. Full per-rank logs remain alongside the summary. Validation failures are collected; timeout/signal/setup failures abort. Performance uses raw device ticks and cannot directly be compared with archived SIMD ns/GB/s until the clock units are aligned.

Host tests cover accounting, all72 geometries, script orchestration and full example syntax with SDK declarations substituted. Target CANN compilation and NPU behavior remain separately verified.

## Explicit Group argument

`Hcomm<>` does not retain a Group. Pass the same `group` to handle creation, every batch append, and `BatchCommit`. Membership, rank, and size must remain unchanged for the handle lifetime. Overloads without a Group are for single-lane handles; `Drain` and CompletionSet APIs are unchanged.
