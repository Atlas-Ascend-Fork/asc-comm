# Hcomm Jetty Write Sample

## Overview

This sample demonstrates an AIV kernel submitting WQEs directly to the Jetty SQ for point-to-point one-sided communication on multiple NPUs in one host. Every rank runs the same flow: it calls `HcommJetty::Write` and `HcommJetty::WriteValue` once for each of the other ranks, then waits for all of its WQEs with `HcommJetty::Drain`. The host side prepares the communicator, registers memory, and creates the URMA channels; it does not take part in each data-plane operation. Each rank then verifies the data and markers it received, and the parent process reports the overall result.

## Requirements

- Ascend 950PR or Ascend 950DT;
- at least two NPUs on one host;
- 2 to 16 ranks, limited by the number of available NPUs.

Use two ranks for the minimal validation, or pass a larger rank count to verify writes between more peers.

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
cd .. && ./build/hcomm_jetty_write tcp://127.0.0.1:29627 2
```

The first argument is the control-channel address that rank 0 listens on, used for root info exchange and host-side barriers. Use a different port when running several instances in parallel. Before running, make sure the requested number of NPUs is available and that the current user can access them.

## Result

Each rank prints a verification message when the data and markers from all of its peers are correct. A successful two-rank run ends with:

```text
[rank 0] hcomm_jetty_write | Write and WriteValue verified for 1 peer(s) | PASS
[rank 1] hcomm_jetty_write | Write and WriteValue verified for 1 peer(s) | PASS
RESULT | Example=hcomm_jetty_write | Status=PASS
```

`Status=FAIL` in the parent-process summary or an error from any rank means that the Jetty write or data verification did not complete successfully.
