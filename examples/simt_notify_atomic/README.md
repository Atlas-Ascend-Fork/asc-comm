# SIMT Notify/FAA/CAS example

This example validates the SIMT URMA `WriteWithNotifyNbi`, `AtomicFAA`, and `AtomicCAS` interfaces.
Rank 0 submits operations and rank 1 checks the remote results.

## Test modes

- `notify`: submit one Notify and call `Drain`.
- `faa`: submit one FAA, then verify the remote sum and the locally fetched old value.
- `cas`: submit one CAS, then verify the remote swap and the locally fetched old value.
- `single`: submit Notify, FAA, and CAS serially, with one `Drain` after each operation.
- `batch_last`: defer Notify and FAA, then use CAS with `commit=true` to publish the batch.
- `multi_lane`: three lanes defer Notify, FAA, and CAS; after a barrier, lane 0 submits one final Notify to publish the batch.

## Build

```bash
source /usr/local/Ascend/cann/set_env.sh
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

## Run

The run script follows the existing `simt_write` example and accepts an HCCL rank table:

```bash
bash run.sh 2 /path/to/rank_table.json notify
bash run.sh 2 /path/to/rank_table.json faa
bash run.sh 2 /path/to/rank_table.json cas
bash run.sh 2 /path/to/rank_table.json single
bash run.sh 2 /path/to/rank_table.json batch_last
bash run.sh 2 /path/to/rank_table.json multi_lane
```

Expected output:

```text
[rank 0] simt_notify_atomic <mode> PASS
[rank 1] simt_notify_atomic <mode> PASS
```

The example requires at least two Ascend 950PR/950DT devices on the same host and a CANN version that provides the
SIMT URMA APIs. Its rank synchronization uses temporary files shared by the local processes.
