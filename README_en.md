<div align="center">

# asc-comm

<h4>Provides Hcomm communication APIs, AIV direct drive implementation, samples and verification cases for communication scenarios on Ascend AI Processors</h4>

[![docs](https://img.shields.io/badge/docs-repo-blue.svg?style=flat)](./docs)
[![examples](https://img.shields.io/badge/examples-repo-orange.svg?style=flat)](./examples)
[![license](https://img.shields.io/badge/license-CANN_Open_2.0-lightgrey.svg)](./LICENSE)
[![contributing](https://img.shields.io/badge/CONTRIBUTING-teal)](./CONTRIBUTING_en.md)

</div>

## 🔥 Latest News
- [2026/07] Initial release of the asc-comm project

### 🚀 Current Capabilities
- Exposes AICore-side Hcomm point-to-point communication interfaces, covering `Init`, `ReadNbi`, `WriteNbi`, `WriteWithNotifyNbi`, `AtomicFAA`, `AtomicCAS`, `Commit`, `Drain`.
- Provides AIV direct-drive implementations for Hcomm RoCE and UBC_CTP/URMA. Core implementations are located under `src/aicore/hcomm/detail/`.
- Delivers Hcomm UT projects covering RoCE/URMA paths for `ascend950pr_9599_AIV` and basic interface test cases for `ascend910B1_AIC`.
- Supplies the `hcomm_write_read_nbi` sample, demonstrating the point-to-point communication workflow of `WriteNbi` and `ReadNbi` under the AIV direct-driven URMA scenario, including Host-side resource preparation required to run the sample.

### 📖 Documentation
- Added [Quick Start](./docs/quick_start_en.md), [Build & Test](./docs/en/guide/build_and_test.md), [Third-party Dependencies & Compatibility](./docs/en/guide/dependencies.md).
- Added [Hcomm Usage Guide](./docs/en/guide/hcomm_usage.md) and [API Reference](./docs/en/api/README.md), covering all currently published Hcomm interfaces.
- Added [Samples Directory](./examples/README_en.md), serving as the entry for AIV direct-drive Hcomm invocations and end-to-end communication samples.

For detailed information on all historical releases and updates, please refer to [CHANGELOG.md](./CHANGELOG_en.md).

## 🚀 Overview
asc-comm is an open-source repository targeting communication scenarios on Ascend AI Processors. It hosts publicly exposed AICore APIs, AIV direct-drive device-side implementations, API documentation, samples and verification suites.

The primary public capability is `AscendC::Hcomm`, designed for point-to-point data communication paths on operator Kernel side. Users select communication protocols via the `AscendC::Hcomm` template, specify communication channels with `ChannelHandle`, and submit communication tasks using non-blocking read/write interfaces. Tasks can be explicitly `Commit`ted on demand, and completion can be awaited via `Drain`.

### Data Plane Capabilities
| Capability | Status |
| --- | --- |
| Public AICore Hcomm Interfaces | Kernel-side `Init`, `ReadNbi`, `WriteNbi`, `WriteWithNotifyNbi`, `AtomicFAA`, `AtomicCAS`, `Commit`, `Drain` available. |
| AIV Direct Drive Implementation | Implementations for Hcomm RoCE and UBC_CTP/URMA are provided; core code resides in `src/aicore/hcomm/detail/`. |
| AIV Direct-drive Sample Supporting Workflow | `hcomm_write_read_nbi` includes communication domain creation, communication memory registration, P2P channel creation and remote memory acquisition required for AIV direct-driven URMA communication. |
| Protocol Features | `COMM_PROTOCOL_ROCE`: read/write, commit and wait; `COMM_PROTOCOL_UBC_CTP`: read/write, write-with-notify, atomic operations, commit and wait. |
| UT Verification | UTs cover RoCE/URMA paths on `ascend950pr_9599_AIV` and basic interface cases for `ascend910B1_AIC`. |
| AIV Direct-drive Samples | `hcomm_write_read_nbi` demonstrates symmetric two-card AIV direct-driven URMA `WriteNbi`/`ReadNbi` communication and result validation. |

### How to Use Hcomm Interfaces
Include the following header when invoking Hcomm on the Kernel side:
```cpp
#include "hcomm/hcomm.h"
```

Basic invocation workflow:
1. Instantiate an `AscendC::Hcomm` object and select the communication protocol.
2. Call `Init` to initialize temporary workspace.
3. Submit communication tasks via `ReadNbi`, `WriteNbi`, `WriteWithNotifyNbi`, `AtomicFAA` or `AtomicCAS`.
4. If `commit = false` is set during task submission, invoke `Commit` to explicitly submit pending communication tasks.
5. Call `Drain` to wait for all communication tasks on the channel to complete.

Protocol capability matrix:
| Protocol | Capability Description |
| --- | --- |
| `COMM_PROTOCOL_ROCE` | RoCE point-to-point path. Supports `ReadNbi`, `WriteNbi`, `Commit`, `Drain`. `WriteWithNotifyNbi` is not supported. |
| `COMM_PROTOCOL_UBC_CTP` | UBC CTP/URMA point-to-point path. Supports `ReadNbi`, `WriteNbi`, `WriteWithNotifyNbi`, `AtomicFAA`, `AtomicCAS`, `Commit`, `Drain`. |

Refer to [Hcomm Usage Guide](./docs/en/guide/hcomm_usage.md) and [API Reference](./docs/en/api/README.md) for detailed parameter constraints and return value descriptions.

## 🔍 Directory Layout
This repository contains AICore communication data plane APIs, device-side implementations, samples, documentation and UT cases for asc-comm. The structure is as follows:
```text
├── cmake                         # CMake helper modules for asc-comm
├── docs                          # Project documentation
├── examples                      # asc-comm API samples
│   └── hcomm_write_read_nbi      # Two-card P2P communication sample for AIV direct-driven URMA Hcomm
├── include                       # asc-comm API declarations
│   ├── aicore/hcomm              # Public AICore Hcomm interfaces
│   ├── ain                       # Reserved directory for AIN-related APIs
├── scripts                       # Utility scripts
├── src                           # asc-comm API implementations
│   ├── aicore/hcomm/detail       # Internal implementation of AICore Hcomm
│   │   ├── common                # Common definitions and utilities for Hcomm
│   │   └── impl                  # Protocol implementations and platform-specific logic
└── tests                         # asc-comm API unit tests
    └── ut/aicore/hcomm           # AICore Hcomm UT project
```

## ⚡️ Quick Start
To quickly build the project and run Hcomm UTs, configure the CANN environment first:
```bash
source /usr/local/Ascend/cann/set_env.sh
```

Default build for basic environment validation. AICore Hcomm is header-only; no standalone library will be generated for non-UT builds:
```bash
bash build.sh
```

Build and execute Hcomm unit tests:
```bash
bash build.sh -t
```

To build UTs directly via CMake, specify the CANN third-party library path:
```bash
cmake -S tests/ut -B build/ut-hcomm -DCANN_3RD_LIB_PATH=<third_party_path>
cmake --build build/ut-hcomm
```

See [Quick Start](./docs/quick_start_en.md) and [Build & Test](./docs/en/guide/build_and_test.md) for more details about environment setup, Docker, CANN package installation and UT dependencies.

## 🧰 Clangd / IDE Support
- Install clangd (version 15 or newer recommended).
- When configuring local IDEs, add the CANN header directory and the repository `include/` directory to the index paths.
- Before modifying Hcomm Kernel-side code, run `source /usr/local/Ascend/cann/set_env.sh` to ensure CANN environment variables are loaded.
- For VS Code, combine C/C++ and clangd extensions to enable code navigation, static checking and header indexing.

## 📖 Related Resources
- **Documentation**

  | Document | Description |
  | --- | --- |
  | [Documentation Index](./docs/README_en.md) | Main entry for asc-comm documentation. |
  | [Quick Start](./docs/quick_start_en.md) | Environment setup, source compilation and UT verification. |
  | [API Reference](./docs/en/api/README.md) | List of published asc-comm interfaces. |
  | [Hcomm Usage Guide](./docs/en/guide/hcomm_usage.md) | Basic workflow for Hcomm point-to-point communication interfaces. |
  | [Build & Test](./docs/en/guide/build_and_test.md) | CANN environment, build scripts, UT and sample build instructions. |
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
- Continuously add end-to-end AIV direct-drive Hcomm samples covering more protocol paths and communication interfaces.
- Improve build verification and UT coverage across different products and protocol paths.
- Supplement API constraints, usage guidance and FAQs.

## 📝 Related Links
- [Contribution Guide](./CONTRIBUTING_en.md)
- [Security Statement](./SECURITY_en.md)
- [License](./LICENSE)
