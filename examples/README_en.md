# asc-comm Samples

This directory contains usage samples for asc-comm APIs.

## Sample List
| Sample | Description | Supported Products |
| --- | --- | --- |
| [hcomm_write_read_nbi](./hcomm_write_read_nbi/README_en.md) | Demonstrates AIV Kernel invoking `Hcomm::WriteNbi` and `Hcomm::ReadNbi` over the URMA path in a two-card scenario, with communication result verification. | Ascend 950PR / Ascend 950DT |

## hcomm_write_read_nbi
`hcomm_write_read_nbi` demonstrates the complete AIV direct-driven URMA point-to-point communication workflow, including Host-side communication domain creation, communication memory registration, AIV P2P channel establishment, and Kernel-side invocations of `Init`, `WriteNbi`, `ReadNbi` and `Drain`. This sample does not cover the RoCE path.

The sample runs symmetrically on two cards:
1. The Host creates a communication domain and registers communication buffers for each rank.
2. Establish a P2P channel to the peer rank via `HcclChannelAcquire`.
3. The Kernel writes local data to the remote buffer using `WriteNbi`.
4. The Kernel reads data back from the remote buffer using `ReadNbi`.
5. The Host reads back and verifies results. `test pass!` is printed if both ranks pass validation.

## Build & Run
Navigate to the sample directory and execute:
```bash
source /usr/local/Ascend/cann/set_env.sh
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
./demo
```

You can also launch the two ranks manually:
```bash
# Terminal 1: rank 0
./demo 0 2 tcp://127.0.0.1:29621

# Terminal 2: rank 1
./demo 1 2 tcp://127.0.0.1:29621
```

## Runtime Constraints
- Supported chips: Ascend 950PR / Ascend 950DT. CANN version 9.1.0 or later is required.
- At least two NPUs are required for runtime execution; single-NPU environments only support compilation verification.
- Sample build relies on CANN ASC CMake utilities and links against the CANN `hcomm` library at link time.
