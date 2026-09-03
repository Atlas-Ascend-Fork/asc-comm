# Temporary Batch Performance Comparison

This example benchmarks Write, Read, and WriteWithNotify on one AICore:

- `simd`: one Jetty; SIMD appends a batch serially and calls BatchCommit once.
- `multi-jetty`: SIMT assigns one lane subgroup to each of 2/4/8 independent Jettys and submits them concurrently.

Each Jetty runs 1024 timed operations of 64 bytes by default. BatchSize equals the lane count per Jetty and covers
`1/8/32/64/128/256`. All subgroups run together when `Jettys × BatchSize` is at most 1024. The `8 × 256`
case uses two waves of four Jetty subgroups on the same core.

`IssueTime/ns` is the cycles for one core to issue all Jetty work divided by `Iterations × Jettys`. The SIMD
single-Jetty path uses Jettys=1. The interval includes Append and BatchCommit but excludes Drain.

```bash
bash build.sh --clean
bash run.sh simd
bash run.sh multi-jetty
bash run.sh all
```

Use `--api write|read|notify|all` for a smaller run, for example:

```bash
bash run.sh multi-jetty --api write --batch-sizes 32,128 --jetty-counts 2,8
```

Each result line reports only Path, API, DataSize, BatchSize, Lanes, Cores, Waves, Jettys, DBCount,
Bandwidth, IssueTime, and Status.
