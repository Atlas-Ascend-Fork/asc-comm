# Documentation Contribution Guide

## Overview
The asc-comm documentation system covers repository overview, Quick Start, Build & Test, API References, usage guides and sample descriptions. Developers may submit PRs to correct, supplement and optimize documents.

| Document Type | Content | Directory |
| --- | --- | --- |
| Repository Homepage | Project overview, directory structure, entry links to major documents | `README_en.md` |
| Quick Start | Environment preparation, source download, build and UT verification | `docs/quick_start_en.md` |
| Build & Dependencies | Build scripts, CMake entry, third-party dependency descriptions | `docs/en/guide/` |
| API References | API functionality, prototypes, parameters, return values and constraints | `docs/en/api/` |
| Usage Guides | API invocation workflow, protocol capabilities and notes | `docs/en/guide/` |
| Sample Documentation | Sample directory entry, runtime prerequisites and verification instructions | `examples/README_en.md`, `examples/*/README_en.md` |

## Contribution Scenarios
### Document Correction
If you discover broken links, incorrect paths, non-executable commands, inaccurate parameter values, missing constraints and other issues:
1. Create a `Documentation | Documentation Feedback` Issue, describing the location of the problem and expected revisions.
2. Type `/assign` or `/assign @yourself` in the comment area to assign the Issue to yourself.
3. Submit a PR after fixing the issue, and describe the verification method in the PR.

### Document Supplement
To add API descriptions, build instructions, dependency notes, sample explanations or FAQs:
1. Create a `Requirement | Feature Request` Issue describing the content to be added and applicable scenarios.
2. Add new content following the writing specifications defined in this document.
3. Update relevant entry documents synchronously to avoid orphaned pages.

### Sample Documentation Addition
When adding new samples, complete supporting documentation that clearly states sample purpose, prerequisites, build & runtime procedures and verification commands.

## Writing Specifications
### General Rules
| Rule | Requirement |
| --- | --- |
| Consistency with Code | Directory structure, build commands, API names, function prototypes and return values must match the actual repository content. |
| Clear Scope | Explicitly state prerequisites and applicable scope for features that only work under specific conditions, along with verification methods. |
| Verification-oriented | Quick Start, Build & Test and sample documents shall clarify which operations users can execute directly and which cannot. |
| Valid Links | Update entry pages after adding new documents, and verify all relative links and image paths. |
| Link on First Mention | Add hyperlinks when referencing concepts or APIs defined in other documents for the first time; repeated mentions do not require duplicate links. |

### Document Structure
New dedicated documents are recommended to include the following sections:
1. Background or applicable scope
2. Prerequisites and dependencies
3. Operation steps or API usage workflow
4. Constraints and applicable scope
5. Verification methods
6. Links to related documents

### API-related Documentation
Content related to API behaviors must be consistent with public header files under `include/`. Descriptions about protocol capabilities, address constraints, alignment requirements, invocation order or chip differences shall be updated in both API references and usage guides.

### Sample-related Documentation
Sample documentation shall cover:
- Sample purpose and covered APIs
- Sample file structure
- Whether the sample can be built or run independently
- Build, run and verification commands
- Mandatory prerequisites and resource preparation

## Common Modification Scenarios
- New APIs: Update API references, usage guides, sample documentation and document entry pages synchronously.
- New samples: Update `examples/README_en.md` with sample purpose, prerequisites and verification steps.
- Build workflow changes: Update Quick Start and Build & Test documents.
- New dependencies: Update third-party dependency documentation, third-party software inventory and Notice file.
- Directory restructuring: Update `README_en.md`, docs entry pages and all affected relative links.

## Pre-PR Self-check Checklist
- [ ] Document descriptions align with current repository code, directories and scripts.
- [ ] Markdown tables, code blocks, heading hierarchy and lists are properly formatted.
- [ ] Relative links and image paths resolve correctly from the location of the document.
- [ ] Command examples are executable, or clearly marked as illustrative snippets only.
- [ ] API prototypes, parameters and return values match public header files.
- [ ] Sample documentation includes prerequisites, build/run procedures and verification steps.
- [ ] New dependencies are reflected in the third-party software inventory and Notice.
- [ ] Features not yet merged into code or documents are not described as supported.

## Documentation Navigation & Link Relationships
asc-comm documents form a navigation network consisting of entry pages, guides, API references and sample materials. When adding or modifying documents, follow the principle: **who references content owned by another document adds the corresponding link**.
```text
README_en.md ──Documentation Index──→ docs/README_en.md
             ──Quick Start──→ docs/quick_start_en.md
             ──Build & Test──→ docs/en/guide/build_and_test.md
             ──API Reference──→ docs/en/api/README.md
             ──Samples──→ examples/README_en.md

docs/README_en.md ──Quick Start──→ docs/quick_start_en.md
                  ──Usage Guide──→ docs/en/guide/hcomm_usage.md
                  ──Build & Test──→ docs/en/guide/build_and_test.md
                  ──API Reference──→ docs/en/api/README.md
                  ──Samples──→ examples/README_en.md

docs/en/guide/ ──First API reference──→ docs/en/api/README.md
                ──Sample reference──→ examples/README_en.md

docs/en/api/ ──Usage workflow──→ docs/en/guide/hcomm_usage.md
             ──Invocation sample──→ examples/hcomm_write_read_nbi/README_en.md
```

## More Information
- API Documentation Contribution Guide: [api_contributing_en.md](./api_contributing_en.md)
- asc-comm Contribution Guide: [CONTRIBUTING_en.md](../CONTRIBUTING_en.md)
