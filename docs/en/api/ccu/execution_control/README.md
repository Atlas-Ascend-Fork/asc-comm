# Introduction

<!-- md-trans-meta sourceCommit=5f8b41dd145ea0a95acdd2f2f2ea4995d681fdd0 translatedAt=2026-11-04T10:44:13.331Z pushedAt=2026-11-09T06:47:55.018Z nl=2 -->

Flow control APIs within the CCU kernel are provided for task orchestration, such as conditional branching, concurrent loops, and function blocks. The three subclasses are selected based on applicable scenarios:

| Subclass | Application Scenario | API |
| --- | --- | --- |
| Software branch/loop | Complex control flow logic with nesting, where any CCU API can be used inside the loop body. | [CCU_IF](CCU_IF.md), [CCU_ELSE](CCU_ELSE.md), [CCU_WHILE](CCU_WHILE.md), [CCU_DO](CCU_DO.md) |
| Hardware loop | Large numbers of iterations with the same structure, where the loop body contains only local data movement operations, minimizing instruction overhead. | [loop](loop.md), [loop_group](loop_group.md) |
| Function blocks | The same logic is called twice or more within the kernel, generating only one copy of instructions to save instruction space. | [func](func.md), [call_func](call_func.md) |

## Working Mechanism in the Two Phases

Flow control APIs are defined and synthesized during the compilation phase, and driven by the CCU hardware during the delivery phase. The three subclasses address different orchestration needs: software branches organize execution paths through flexible conditional jumps, hardware loops implement iteration in hardware to minimize instruction overhead, and function blocks generate a single copy of instructions for the same logic that is reused across multiple call sites. The three are also often combined—orchestrating loops within branches, and delegating the loop body of a loop or the logic of a function block to `func`.

The behavior of the APIs in each subclass across the two phases is as follows.

**Software branch**: `CCU_IF` is a macro that is expanded along with the kernel function body during the compilation phase, where API calls within the **then** block are recorded as instructions. During the delivery phase, the hardware evaluates the condition; if it is satisfied, the **then** block is executed, otherwise execution jumps to the block exit.

**Hardware loop**: The API is divided into two steps: definition and synthesis.

1. Construct [loop](loop.md): execute the loop body lambda carried by `func`, and record the logic as a single copy of loop body instructions.
2. Construct [loop_group](loop_group.md): write the iteration parameters (loop count and address offset) and replication parameters of each `loop`, and synthesize the hardware loop instruction. A `loop` must be added to a `loop_group`; otherwise, no loop effect is produced.

During the delivery phase, when the LoopEngine (the concurrent execution engine of the CCU) reaches a loop instruction, it iteratively executes the loop body according to the configured parameters, updating the data address by the offset in each iteration. Multiple copies of the loop body replicated by `loop_group` execute in parallel.

**Function block**: The API is divided into two steps: definition and call.

1. Construct [func](func.md): only the lambda is recorded, and no instructions are generated. `func` serves two purposes—as a loop body passed to `loop`, or as a function block called by [call_func](call_func.md).
2. Call [call_func](call_func.md): On the first call, the lambda is executed, synthesized into a single function block instruction, and a jump instruction is appended; subsequent calls only append a jump instruction.

In the delivery phase, when the hardware executes the jump instruction, it enters the function block, and a single instruction is reused in multiple places.

> [!NOTE] Note
>
> - Nesting a hardware loop inside a software branch is allowed;
> - A software branch cannot be used inside the loop body of a hardware loop. The code does not report an error, but the hardware exhibits undefined behavior.

## API List

- [CCU_IF](CCU_IF.md)
- [CCU_ELSE](CCU_ELSE.md)
- [CCU_WHILE](CCU_WHILE.md)
- [CCU_DO](CCU_DO.md)
- [loop](loop.md)
- [loop_group](loop_group.md)
- [func](func.md)
- [call_func](call_func.md)
