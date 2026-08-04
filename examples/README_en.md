# asc-comm Samples

This directory contains usage samples for asc-comm APIs.

## Sample List
| Sample | Description | Supported Products |
| --- | --- | --- |
| [hcomm_write_read_nbi](./hcomm_write_read_nbi/README_en.md) | Demonstrates AIV Kernel invoking `Hcomm::WriteNbi` and `Hcomm::ReadNbi` over the URMA path in a ring topology, with communication result verification. | Ascend 950PR / Ascend 950DT |

## hcomm_write_read_nbi
`hcomm_write_read_nbi` demonstrates the complete AIV direct-driven URMA point-to-point communication workflow, including Host-side communication domain creation, communication memory registration, AIV P2P channel establishment, and Kernel-side invocations of `Init`, `WriteNbi`, `ReadNbi` and `Drain`. This sample does not cover the RoCE path.

The sample starts ranks through automatic multi-process fork. It uses two ranks by default, and the rank count can be specified with `./demo [nranks]`. At runtime, each rank communicates with its previous and next neighbors in a ring topology:

1. The Host creates a communication domain and registers communication buffers for each rank.
2. The Host builds a TCP ring topology: `prev = (rank - 1 + nranks) % nranks`, `next = (rank + 1) % nranks`.
3. AIV P2P channels to neighboring ranks are established via `HcclChannelAcquire`.
4. The Kernel writes local data to `next` through `WriteNbi`, and reads data from `next` through `ReadNbi`.
5. The Host reads back and verifies results. `test pass!` is printed if all ranks pass validation.

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

You can also specify the rank count:

```bash
# Explicit two-rank run
./demo 2

# Four-rank ring topology
./demo 4
```

## Runtime Constraints
- Supported chips: Ascend 950PR / Ascend 950DT. CANN version 9.1.0 or later is required.
- At least two NPUs are required for runtime execution; single-NPU environments only support compilation verification.
- Sample build relies on CANN ASC CMake utilities and links against the CANN `hcomm` library at link time.
