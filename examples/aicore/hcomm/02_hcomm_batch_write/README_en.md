# Hcomm Batch Write Sample

## Overview

This sample demonstrates Hcomm batch writes on multiple NPUs in one host. Rank 0 writes data to the other ranks by preparing a group of write operations, submitting them together, and waiting for completion. The receiving ranks verify the data, and the parent process reports the overall result.

## Requirements

- Ascend 950PR or Ascend 950DT;
- at least two NPUs on one host;
- 2 to 16 ranks, limited by the number of available NPUs.

Use two ranks for the minimal validation, or pass a larger rank count to verify writes to multiple destinations.

## Build and Run

Set up the CANN environment first:

```bash
source ${install_path}/cann/set_env.sh
```

Build and run with two NPUs by default, or pass a rank count from `2` to `16`:

```bash
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
cd .. && ./build/hcomm_batch_write tcp://127.0.0.1:29624 4
```

The final command starts four ranks. Before running, make sure the requested number of NPUs is available and that the current user can access them.

## Result

Each receiving rank prints a verification message when its data is correct. A successful four-rank run ends with:

```text
[rank 1] hcomm_batch_write | received and verified data from rank 0 | PASS
[rank 2] hcomm_batch_write | received and verified data from rank 0 | PASS
[rank 3] hcomm_batch_write | received and verified data from rank 0 | PASS
RESULT | Example=hcomm_batch_write | Status=PASS
```

`Status=FAIL` in the parent-process summary or an error from any rank means that the batch write or data verification did not complete successfully.
