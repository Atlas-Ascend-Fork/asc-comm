# API Documentation Contribution Guide

This document describes the requirements for supplementing and modifying the asc-comm API documentation. When adding or modifying a public API, synchronously update the corresponding API documentation, usage guide, sample, and test description.

## Scope

Applies to the public API reference documentation under `docs/zh/api/`, as well as the usage guides and sample documentation directly related to API behavior.

## Document Structure

It is recommended that a new API document include the following sections:

- Functionality: Describe the purpose, usage scenarios, and applicable scope of the API.
- Function Prototype: Keep it consistent with the declaration in the public header file.
- Parameter Description: List parameter names, input/output attributes, units, and constraints.
- Template Parameters: For template interfaces, describe the default values, applicable protocols, or platform differences.
- Return Values: Describe success, failure, and common failure conditions.
- Constraints: Describe the invocation order, address requirements, alignment requirements, supported protocol range, and dependencies on supporting resources.

## Writing Requirements

- API names, parameter names, default template parameters, and return values must be based on the code.
- When a document involves protocol capabilities, clearly specify the supported range, for example `COMM_PROTOCOL_ROCE` and `COMM_PROTOCOL_UBC_CTP`.
- If an interface involves communication channels, registered memory, or operator project capabilities, clearly describe the resource preparation requirements in the constraints section.
- Sample code should reflect a verifiable invocation method; for fragments that cannot run independently, clearly describe the prerequisites.
- When modifying API behavior, synchronously update `docs/zh/api/README.md`, the related guides, examples, and UT descriptions.

## Recommended Checks

It is recommended to complete the following checks before submission:

- Confirm that the function prototype in the document is consistent with the public header file under `include/`.
- Confirm that link paths can be correctly navigated from the current document location.
- Confirm that newly added constraints are consistent with existing UTs or implementation logic.
- If new third-party dependencies or directly packaged content are introduced, synchronously check the third-party software list and Notice.
