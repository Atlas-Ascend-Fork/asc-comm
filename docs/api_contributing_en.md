# API Documentation Contribution Guide

This document specifies requirements for supplementing and modifying asc-comm API documentation. When adding or modifying public APIs, corresponding API references, usage guides, samples and test descriptions shall be updated synchronously.

## Scope

Applicable to public API reference documents under `docs/en/api/`, as well as usage guides and sample documents directly related to API behaviors.

## Document Structure

New API documents are recommended to contain the following sections:

- Function Description: Introduce the purpose, usage scenarios and applicable scope of the API.
- Function Prototype: Keep consistent with declarations in public header files.
- Parameter Description: List parameter names, input/output attributes, units and constraints.
- Template Parameters: For template APIs, explain default values, supported protocols or platform differences.
- Return Value: Describe success status, failure status and common failure conditions.
- Constraints: Specify invocation order, address requirements, alignment rules, supported protocol scope and dependent resources.

## Writing Requirements

- API names, parameter names, default template parameters and return values must conform to source code definitions.
- When describing protocol capabilities in documents, clearly state supported scope such as `COMM_PROTOCOL_ROCE` and `COMM_PROTOCOL_UBC_CTP`.
- If an API involves communication channels, registered memory or operator capabilities, clarify resource preparation requirements in constraints.
- Sample code shall reflect verifiable invocation patterns. Explicitly state prerequisites for code snippets that cannot run independently.
- When changing API behaviors, synchronously update `docs/api/README.md`, relevant guides, examples and UT descriptions.

## Pre-submission Checklist

Complete the following checks before submitting changes:

- Verify that function prototypes in documents match public header files under `include/`.
- Ensure all link paths can be resolved correctly relative to the current document.
- Confirm newly added constraints are consistent with existing UTs and implementation logic.
- If new third-party dependencies or bundled artifacts are introduced, update the third-party software inventory and Notice accordingly.
