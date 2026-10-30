# Documentation Contribution Guide

## Overview

The asc-comm documentation system consists of the repository entry, quick start, build and test, API reference, usage guide, and sample description. Developers can correct, supplement, and optimize the documentation by submitting PRs.

| Document Type | Content | Document Directory |
| --- | --- | --- |
| Repository Entry | Project overview, directory structure, and common document entries | `README_en.md` |
| Quick Start | Environment setup, source download, build, and UT verification | `docs/quick_start_en.md` |
| Build and Dependencies | Build scripts, CMake entry, and third-party dependency description | `docs/en/compilation` |
| API Reference | Interface functionality, prototype, parameters, return values, and constraints | `docs/zh/api (Chinese)` |
| Usage Guide | API invocation workflow, protocol capabilities, and precautions | `docs/zh/guide (Chinese)` |
| Sample Description | Sample directory entry, runtime boundaries, and verification description | `examples/README.md`, `examples/*/README.md` |

## Contribution Scenarios

### Document Correction

If you find broken links, incorrect paths, non-executable commands, inaccurate parameter values, or missing constraints in the documentation:

1. Create an Issue of the `Documentation | 文档反馈` type, describing the problem location and the expected change.
2. Enter `/assign` or `/assign @yourself` in the comment box to assign the Issue to yourself.
3. Submit a PR after fixing the issue, and describe the verification method in the PR.

### Document Supplement

If you need to supplement interface descriptions, build instructions, dependency descriptions, sample descriptions, or FAQs:

1. Create an Issue of the `Requirement | 需求建议` type, describing the content to be supplemented and the applicable scenarios.
2. Supplement the documentation according to the writing specifications in this document.
3. Synchronously update the related entry documents to avoid leaving the newly added content as an isolated page.

### Sample Documentation Supplement

When adding a new sample, synchronously supplement the sample description, specifying the sample purpose, prerequisites, build and run method, and verification commands.

## Writing Specifications

### General Specifications

| Rule | Requirement |
| --- | --- |
| Consistent with code | The directory structure, build commands, interface names, function prototypes, and return values should be consistent with the actual content of the current repository. |
| Clear applicable scope | For capabilities that require prerequisites or are only applicable to specific scenarios, clearly describe the applicable scope and verification method. |
| Verification-oriented | Quick start, build and test, and sample documentation should describe what users can and cannot directly execute. |
| Valid links | After adding a new document, synchronously update the entry page and check that relative links and image paths are valid. |
| Add links on first reference | Add a link when first referring to a concept or API maintained by another document; subsequent references do not need to repeat the link. |

### Document Structure

It is recommended that a new topical document include the following content:

1. Background or applicable scope.
2. Prerequisites and dependencies.
3. Operation steps or interface usage workflow.
4. Constraints and applicable scope.
5. Verification method.
6. Related document links.

### API-related Documentation

API behavior documentation should be consistent with the public header files under `include/`. When protocol capabilities, address constraints, alignment requirements, invocation order, or product differences are involved, synchronously describe them in the API reference and usage guide.

### Sample-related Documentation

Sample documentation should describe:

- The sample purpose and the APIs it covers.
- The sample file structure.
- Whether the sample can be built or run independently.
- The build, run, and verification commands.
- The necessary prerequisites and resource preparation.

## Common Modification Scenarios

- Add an interface: synchronously update the API reference, usage guide, sample description, and document entries.
- Add a sample: synchronously update `examples/README.md`, describing the sample purpose, prerequisites, and verification method.
- Modify the build workflow: synchronously update the quick start and build and test documents.
- Add a dependency: synchronously update the third-party dependency description, the third-party software list, and the Notice.
- Adjust the directory structure: synchronously update the README, docs entries, and related relative links.

## Pre-submission Self-check Checklist

- [ ] The document description is consistent with the current repository code, directory, and scripts.
- [ ] Markdown tables, code blocks, heading levels, and list formats are correct.
- [ ] Relative links and image paths can be correctly navigated from the current document location.
- [ ] Command examples are executable, or clearly marked as examples only.
- [ ] API function prototypes, parameters, and return values are consistent with the public header files.
- [ ] The sample description marks the prerequisites, build and run method, and verification method.
- [ ] Newly added dependencies are synchronously reflected in the third-party software list and Notice.
- [ ] Capabilities whose code or documentation has not been merged are not described as already supported.

## Documentation System and Link Relationships

The asc-comm documentation forms a navigation relationship through entry documents, guides, API references, and sample descriptions. When adding or modifying a document, follow the principle that "whoever refers to content maintained by another document adds the link".

```text
README.md ──document entry──→ docs/README.md
          ──quick start──→ docs/quick_start.md
          ──build & test──→ docs/zh/compilation/build_and_test.md
          ──API reference──→ docs/zh/api/README.md
          ──sample entry──→ examples/README.md

docs/README.md ──quick start──→ docs/quick_start.md
               ──usage guide──→ docs/zh/guide/hcomm_usage.md
               ──build & test──→ docs/zh/compilation/build_and_test.md
               ──API reference──→ docs/zh/api/README.md
               ──sample entry──→ examples/README.md

docs/zh/guide/ ──first API reference──→ docs/zh/api/README.md
               ──related samples──→ examples/README.md

docs/zh/api/ ──usage workflow──→ docs/zh/guide/hcomm_usage.md
             ──invocation example──→ examples/aicore/hcomm/01_hcomm_urma_write_read_nbi/README.md
```

## More Information

- API documentation contribution guide: [api_contributing.md](./api_contributing.md)
- asc-comm contribution guide: [CONTRIBUTING_en.md](../../../CONTRIBUTING_en.md)
