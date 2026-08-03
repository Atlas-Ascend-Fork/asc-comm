# Changelog

This document records major changes to the asc-comm repository.

## Unreleased
### Added
- Added the asc-comm development run package for backing up and replacing public and detail headers by AICore module in an existing CANN environment, with uninstall restoration support.
- Added project documentation index, Quick Start, Build & Test, and Third-party Dependencies & Compatibility documents.
- Added API reference documents for AICore Hcomm point-to-point communication interfaces, including `Hcomm`, `Init`, `ReadNbi`, `WriteNbi`, `WriteWithNotifyNbi`, `AtomicFAA`, `AtomicCAS`, `Commit`, `Drain`.
- Added the AIV direct-driven URMA Hcomm sample `hcomm_write_read_nbi`, covering two-card point-to-point communication workflow with `WriteNbi` and `ReadNbi`.
- Added Issue templates, contribution guide and documentation contribution specifications.

### Notes
- The currently exposed capabilities mainly cover AICore Hcomm point-to-point communication interfaces.
