# Introduction

<!-- md-trans-meta sourceCommit=c91963b8b6f91b9b3ce4e0ac6f43937ae3995108 translatedAt=2026-11-04T10:28:19.159Z pushedAt=2026-11-09T06:47:55.006Z nl=2 -->

`variable` is only a scalar placeholder at the compilation phase and carries no valid value by itself. It must rely on the APIs in this section to establish associations with external data. To this end, this section provides a set of APIs for reading and writing between the `variable` inside a CCU kernel and external scalar values: they support loading external data into a `variable` and storing the value of a `variable` to the outside.

## API List

- [load_arg](load_arg.md): Declared during the compilation phase, this API loads parameters from `taskArgs` injected by Host Launch during the dispatch phase.
- [load](load.md): Reads one or more `uint64_t` values from an on-chip memory address and writes them to `variable`. Supports immediate addresses and indirect addressing via `variable`.
- [store](store.md): Writes the `uint64_t` values of one or more `variable` to an on-chip memory address. Supports immediate addresses and indirect addressing via `variable`.
