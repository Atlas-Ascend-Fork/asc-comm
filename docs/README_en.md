# Project Documentation

English | [简体中文](./README.md)

## Directory Structure

```text
docs/
├── zh/api/                    # Chinese API reference documentation
├── zh/experimental/           # CCU 2.0 experimental documentation (Chinese)
├── zh/guide/                  # Chinese usage guides
│   ├── index.md               # Usage guide index
│   ├── communication_op_dev/  # Communication operator development guide
│   └── programming_model/     # Programming model
├── zh/compilation/            # Chinese build and third-party dependency documentation
├── zh/contributing/           # Chinese contribution guide
├── en/api/                    # English API reference documentation
├── en/guide/                  # English usage guides
├── en/compilation/            # English build and third-party dependency documentation
├── en/contributing/           # English contribution guide
├── quick_start.md             # Quick start
└── quick_start_en.md          # English quick start
```

> [!NOTE]Note
> For the following product documentation directories, the Chinese versions are the authoritative source. The English versions are translated and reviewed by a professional team before release. Due to the translation and release cycle, the English documents may lag behind the Chinese documents. If there are any discrepancies, the Chinese versions shall prevail.
>
> - [`./zh/api`](./zh/api/)
> - [`./zh/guide`](./zh/guide/)
> - [`./en/api` (translated from `./zh/api`)](./en/api/)
> - [`./en/guide` (translated from `./zh/guide`)](./en/guide/)

## Document Index

| Document | Content |
| --- | --- |
| [Quick Start](./quick_start_en.md) | Environment setup, source compilation, and UT verification for asc-comm. |
| [Communication Programming](./en/guide/index.md) | Entry point for programming model and communication operator development guides. |
| [API Reference](./en/api/index.md) | List of currently published asc-comm interfaces. |
| [CCU Experimental Documentation (Chinese)](./zh/experimental/README.md) | CCU 2.0 kernel development, compilation, and execution guide. |
| [Build & Test](./en/compilation/build_and_test.md) | CANN environment, development run package, UT build, and sample build instructions. |
| [Third-party Dependencies & Compatibility](./en/compilation/dependencies.md) | Direct dependencies, sample runtime dependencies, installation configuration, and integration dependency boundaries. |
| [API Documentation Contribution Guide](./en/contributing/api_contributing.md) | Structure, constraints, and checklist for adding or updating API documentation. |
| [Documentation Contribution Guide](./en/contributing/doc_contributing.md) | Specification for supplementing README, docs, examples, and other documentation. |
| [Contribution Guide](../CONTRIBUTING_en.md) | Issue, development, check, and PR submission workflow. |
| [Samples](../examples/README_en.md) | Entry point for asc-comm API samples, including the AIV direct-drive URMA WriteNbi/ReadNbi point-to-point communication sample. |
