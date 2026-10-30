# Project Documentation

English | [简体中文](./README.md)

## Directory Structure

```text
docs/
├── zh/api/              # Chinese API reference documentation
├── zh/guide/            # Chinese usage guides
├── zh/programming_model/ # Chinese programming model documentation
├── zh/compilation/      # Chinese build and third-party dependency documentation
├── zh/contributing/     # Chinese contribution guide
├── en/compilation/      # English build and third-party dependency documentation
├── en/contributing/     # English contribution guide
├── quick_start.md       # Quick start
└── quick_start_en.md    # English quick start
```

## Document Index

| Document | Content |
| --- | --- |
| [Quick Start](./quick_start_en.md) | Environment setup, source compilation, and UT verification for asc-comm. |
| [Programming Model (Chinese)](./zh/programming_model/overview.md) | Ascend C communication programming paradigm, abstract hardware architecture, and classification and selection of communication modes. |
| [API Reference (Chinese)](./zh/api/README.md) | List of currently published asc-comm interfaces. |
| [Hcomm Usage Guide (Chinese)](./zh/guide/hcomm_usage.md) | Basic workflow for Hcomm point-to-point communication interfaces. |
| [Build & Test](./en/compilation/build_and_test.md) | CANN environment, development run package, UT build, and sample build instructions. |
| [Third-party Dependencies & Compatibility](./en/compilation/dependencies.md) | Direct dependencies, sample runtime dependencies, installation configuration, and integration dependency boundaries. |
| [API Documentation Contribution Guide](./en/contributing/api_contributing.md) | Structure, constraints, and checklist for adding or updating API documentation. |
| [Documentation Contribution Guide](./en/contributing/doc_contributing.md) | Specification for supplementing README, docs, examples, and other documentation. |
| [Contribution Guide](../CONTRIBUTING_en.md) | Issue, development, check, and PR submission workflow. |
| [Samples](../examples/README_en.md) | Entry point for asc-comm API samples, including the AIV direct-drive URMA WriteNbi/ReadNbi point-to-point communication sample. |
