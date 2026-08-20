# Changelog

This document records major changes to the asc-comm repository.

## Unreleased
### Added
- Added project documentation index, Quick Start, Build & Test, and Third-party Dependencies & Compatibility documents.
- Added API reference documents for AICore asccomm point-to-point communication interfaces, including `asccomm`, `init`, `read_nbi`, `write_nbi`, `write_with_notify_nbi`, `atomic_faa`, `atomic_cas`, `commit`, `drain`.
- Added the AIV direct-driven URMA asccomm sample `hcomm_write_read_nbi`, covering two-card point-to-point communication workflow with `write_nbi` and `read_nbi`.
- Added Issue templates, contribution guide and documentation contribution specifications.

### Notes
- The currently exposed capabilities mainly cover AICore asccomm point-to-point communication interfaces.
