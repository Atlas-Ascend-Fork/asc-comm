<div align="center">

# asc-comm

<h4>Provides asccomm and ain communication APIs, AIV direct drive implementation, samples and verification cases for communication scenarios on Ascend AI Processors</h4>

[![docs](https://img.shields.io/badge/docs-repo-blue.svg?style=flat)](./docs)
[![examples](https://img.shields.io/badge/examples-repo-orange.svg?style=flat)](./examples)
[![license](https://img.shields.io/badge/license-CANN_Open_2.0-lightgrey.svg)](./LICENSE)
[![contributing](https://img.shields.io/badge/CONTRIBUTING-teal)](./CONTRIBUTING_en.md)

</div>

## 🔥 Latest News
- [2026/07] Initial release of the asc-comm project

### 🚀 Current Capabilities
- Exposes AICore-side asccomm point-to-point communication interfaces, covering `init`, `read_nbi`, `write_nbi`, `write_with_notify_nbi`, `atomic_faa`, `atomic_cas`, `commit`, `drain`.
- Provides AIV direct-drive implementations for asccomm RoCE and UBC_CTP/URMA. Core implementations are located under `src/aicore/hcomm/`.
- Exposes AICore-side ain one-sided communication interfaces, covering `put`, `put_value`, `get`, `signal`, `read_signal`, `wait_signal`, `flush`, `flush_async`, `wait`, and the `ain_barrier_session` collective synchronization primitive. Core implementations are located under `src/aicore/ain/`.
- Delivers asccomm UT projects covering RoCE/URMA paths for `ascend950pr_9599_AIV` and basic interface test cases for `ascend910B1_AIC`.
- Delivers ain UT project covering `put`/`get`/`signal`/`read_signal`/`wait_signal`/`BarrierSession` test cases on the `ascend950pr_9599_AIV` URMA path.
- Supplies the `hcomm_write_read_nbi` sample, demonstrating the point-to-point communication workflow of `write_nbi` and `read_nbi` under the AIV direct-driven URMA scenario, including Host-side resource preparation required to run the sample.
- Provides SIMT URMA `write_with_notify_nbi`, `atomic_faa`, and `atomic_cas`, together with corresponding functional and performance samples.

### 📖 Documentation
- Added [Quick Start](./docs/quick_start_en.md), [Build & Test](./docs/en/guide/build_and_test.md), [Third-party Dependencies & Compatibility](./docs/en/guide/dependencies.md).
- Added [asccomm Usage Guide](./docs/en/guide/hcomm_usage.md) and [API Reference](./docs/en/api/README.md), covering all currently published asccomm interfaces.
- Added [Samples Directory](./examples/README_en.md), serving as the entry for AIV direct-drive asccomm invocations and end-to-end communication samples.

For detailed information on all historical releases and updates, please refer to [CHANGELOG.md](./CHANGELOG_en.md).

## 🚀 Overview
asc-comm is an open-source repository targeting communication scenarios on Ascend AI Processors. It hosts publicly exposed AICore APIs, AIV direct-drive device-side implementations, API documentation, samples and verification suites.

The public capabilities include `AscendC::asccomm` point-to-point communication and `AscendC::ain` one-sided communication, targeting communication data paths on the operator Kernel side. For asccomm, users select a communication protocol via the `AscendC::asccomm` template, specify a communication channel with `channel_handle`, and submit communication tasks using non-blocking read/write interfaces. Tasks can be explicitly `commit`ted on demand, and completion can be awaited via `drain`. For ain, users issue one-sided `put`/`get`/`signal` operations on symmetric windows via the `AscendC::ain` template, and manage completion through `flush` or `flush_async` + `wait`.

### Data Plane Capabilities
| Capability | Status |
| --- | --- |
| Public AICore asccomm Interfaces | Kernel-side `init`, `read_nbi`, `write_nbi`, `write_with_notify_nbi`, `atomic_faa`, `atomic_cas`, `commit`, `drain` available. |
| Public AICore ain Interfaces | Kernel-side `put`, `put_value`, `get`, `signal`, `read_signal`, `wait_signal`, `flush`, `flush_async`, `wait`, and `ain_barrier_session` synchronization primitive available. |
| AIV Direct Drive Implementation | Implementations for asccomm RoCE and UBC_CTP/URMA are provided; core code resides in `src/aicore/hcomm/`. ain implementation resides in `src/aicore/ain/`. |
| AIV Direct-drive Sample Supporting Workflow | `hcomm_write_read_nbi` includes communication domain creation, communication memory registration, P2P channel creation and remote memory acquisition required for AIV direct-driven URMA communication. |
| Protocol Features | `comm_protocol_roce`: read/write, commit and wait; `comm_protocol_ubc_ctp`: read/write, write-with-notify, atomic operations, commit and wait. |
| UT Verification | UTs cover asccomm RoCE/URMA paths and the ain URMA path on `ascend950pr_9599_AIV`, plus basic interface cases for `ascend910B1_AIC`. |
| AIV Direct-drive Samples | `hcomm_write_read_nbi` demonstrates symmetric two-card AIV direct-driven URMA `write_nbi`/`read_nbi` communication and result validation. |
| SIMT URMA Notify/Atomic Interfaces | Provides `write_with_notify_nbi`, `atomic_faa`, and `atomic_cas`; deferred tasks are published by a subsequent `commit=true` task. |
| SIMT URMA Notify/Atomic Samples | Functional and performance samples cover isolated operations, repeated immediate submission, batch-last, and multi-lane submission. |

### How to Use asccomm Interfaces
Include the following header when invoking asccomm on the Kernel side:
```cpp
#include "hcomm/hcomm.h"
```

Basic invocation workflow:
1. Instantiate an `AscendC::asccomm` object and select the communication protocol.
2. Call `init` to initialize temporary workspace.
3. Submit communication tasks via `read_nbi`, `write_nbi`, `write_with_notify_nbi`, `atomic_faa` or `atomic_cas`.
4. If `commit = false` is set during task submission, invoke `commit` to explicitly submit pending communication tasks.
5. Call `drain` to wait for all communication tasks on the channel to complete.

Protocol capability matrix:
| Protocol | Capability Description |
| --- | --- |
| `comm_protocol_roce` | RoCE point-to-point path. Supports `read_nbi`, `write_nbi`, `commit`, `drain`. `write_with_notify_nbi` is not supported. |
| `comm_protocol_ubc_ctp` | UBC CTP/URMA point-to-point path. Supports `read_nbi`, `write_nbi`, `write_with_notify_nbi`, `atomic_faa`, `atomic_cas`, `commit`, `drain`. |

Refer to [asccomm Usage Guide](./docs/en/guide/hcomm_usage.md) and [API Reference](./docs/en/api/README.md) for detailed parameter constraints and return value descriptions.

### How to Use ain Interfaces
Include the following header when invoking ain on the Kernel side:
```cpp
#include "ain/ain.h"
```

Basic invocation workflow:
1. Instantiate an `AscendC::ain` object, binding it to a communication context index.
2. Issue one-sided read/write via `put`/`put_value`/`get`, or a remote atomic signal operation via `signal`.
3. If `ain_commit_delayed` is set at submission, submission is deferred until a subsequent `ain_commit_immed` task rings the doorbell; otherwise the task is submitted immediately.
4. Completion can be awaited via `flush` or `flush_async` + `wait`.
5. Read or wait on local signals via `read_signal`/`wait_signal`.
6. For collective synchronization, use `ain_barrier_session::Sync` to perform a team-level barrier.

commit mode matrix:
| Mode | Behavior |
| --- | --- |
| `ain_commit_immed` | Assembles the communication task and rings the doorbell immediately, submitting it to the underlying engine. |
| `ain_commit_delayed` | Only assembles the communication task without ringing the doorbell; submission is deferred until a subsequent `ain_commit_immed` task. |

Refer to [API Reference](./docs/en/api/README.md) for detailed parameter constraints and return value descriptions.

## 🔍 Directory Layout
This repository contains AICore communication data plane APIs, device-side implementations, samples, documentation and UT cases for asc-comm. The structure is as follows:
```text
├── cmake                         # CMake helper modules for asc-comm
├── docs                          # Project documentation
├── examples                      # asc-comm API samples
│   ├── hcomm_write_read_nbi      # Two-card P2P communication sample for AIV direct-driven URMA asccomm
│   ├── simt_notify_atomic        # asccomm SIMT URMA Notify/FAA/CAS functional sample
│   └── simt_notify_atomic_perf   # asccomm SIMT URMA Notify/FAA/CAS performance sample
├── include                       # asc-comm API declarations
│   ├── aicore/hcomm              # Public AICore asccomm interfaces
│   └── aicore/ain                # Public AICore ain one-sided communication interfaces
├── scripts                       # Utility scripts
├── src                           # asc-comm API implementations
│   ├── aicore/hcomm              # Internal implementation of AICore asccomm
│   │   ├── common                # Common definitions and utilities for asccomm
│   │   └── impl                  # Protocol implementations and platform-specific logic
│   └── aicore/ain                # Internal implementation of AICore ain
│       └── impl                  # One-sided communication primitive implementations
└── tests                         # asc-comm API unit tests
    └── ut/aicore
        ├── hcomm                 # AICore asccomm UT project
        └── ain                   # AICore ain UT project
```

## ⚡️ Quick Start
To quickly build the project and run UTs, configure the CANN environment first:
```bash
source /usr/local/Ascend/cann/set_env.sh
```

Default build for basic environment validation. AICore asccomm and ain are header-only; no standalone library will be generated for non-UT builds:
```bash
bash build.sh
```

The default build reuses the existing `build/` directory and does not remove build artifacts. To clean it, run:

```bash
bash build.sh --make_clean
```

Build and execute asccomm and ain unit tests:
```bash
bash build.sh -t
```

Build the current asccomm and ain headers into a development run package and install it into an existing CANN environment:

```bash
bash build.sh --pkg
./build_out/cann-asc-comm_1.0.0_linux-<arch>.run --full
```

The packaging script recursively collects headers under `include/aicore/hcomm/`, `src/aicore/hcomm/`, `include/aicore/ain/`, and `src/aicore/ain/`. Its directory mapping and asccomm symbolic links match the current asc-devkit packaging logic. Installation backs up existing target files and link state, while content that did not exist before installation is removed during uninstallation. See [Build & Test](./docs/en/guide/build_and_test.md) for directory mappings, options, and limitations.

To build UTs directly via CMake, specify the CANN third-party library path:
```bash
cmake -S tests/ut -B build/ut-hcomm -DCANN_3RD_LIB_PATH=<third_party_path>
cmake --build build/ut-hcomm
```

See [Quick Start](./docs/quick_start_en.md) and [Build & Test](./docs/en/guide/build_and_test.md) for more details about environment setup, Docker, CANN package installation and UT dependencies.

## 🧰 Clangd / IDE Support
- Install clangd (version 15 or newer recommended).
- When configuring local IDEs, add the CANN header directory and the repository `include/` directory to the index paths.
- Before modifying asccomm Kernel-side code, run `source /usr/local/Ascend/cann/set_env.sh` to ensure CANN environment variables are loaded.
- For VS Code, combine C/C++ and clangd extensions to enable code navigation, static checking and header indexing.

## 📖 Related Resources
- **Documentation**

  | Document | Description |
  | --- | --- |
  | [Documentation Index](./docs/README_en.md) | Main entry for asc-comm documentation. |
  | [Quick Start](./docs/quick_start_en.md) | Environment setup, source compilation and UT verification. |
  | [API Reference](./docs/en/api/README.md) | List of published asc-comm interfaces. |
  | [asccomm Usage Guide](./docs/en/guide/hcomm_usage.md) | Basic workflow for asccomm point-to-point communication interfaces. |
  | [Build & Test](./docs/en/guide/build_and_test.md) | CANN environment, development run package, UT and sample build instructions. |
  | [Third-party Dependencies & Compatibility](./docs/en/guide/dependencies.md) | Direct dependencies, sample runtime dependencies, installation configuration and integration boundaries. |
  | [Samples Directory](./examples/README_en.md) | Entry point for asc-comm API samples. |

- **Contribution Guides**

  | Document | Description |
  | --- | --- |
  | [CANN Community Contribution Guide](https://gitcode.com/cann/community) | General workflow for CANN community Issues and PRs. |
  | [asc-comm Contribution Guide](./CONTRIBUTING_en.md) | Repository-specific rules for Issues, development, checks and PR submission. |
  | [API Documentation Contribution Guide](./docs/api_contributing_en.md) | Structure, constraints and checklist for adding or updating API docs. |
  | [Documentation Contribution Guide](./docs/doc_contributing_en.md) | Specification for maintaining README, docs, examples and other documentation. |

- **Others**

  | Document | Description |
  | --- | --- |
  | [Changelog](./CHANGELOG_en.md) | Release change records. |
  | [Security Statement](./SECURITY_en.md) | Guidelines for vulnerability reporting and handling. |
  | [Third_Party_Open_Source_Software_List.yaml](./Third_Party_Open_Source_Software_List.yaml) | Inventory of third-party open-source software. |
  | [Third_Party_Open_Source_Software_Notice](./Third_Party_Open_Source_Software_Notice) | Notices for third-party open-source software. |

## 📌 Roadmap
- Continuously add end-to-end AIV direct-drive asccomm samples covering more protocol paths and communication interfaces.
- Improve build verification and UT coverage across different products and protocol paths.
- Supplement API constraints, usage guidance and FAQs.

## 📝 Related Links
- [Contribution Guide](./CONTRIBUTING_en.md)
- [Security Statement](./SECURITY_en.md)
- [License](./LICENSE)
