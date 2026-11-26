# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

"""Bind copied legacy batch parsers to the native execution plan.

A native rank executes entries serially in manifest order. The legacy parser
checks the complete capture count and assigns each size_index/call_index;
client connection IDs, exported AICPU streams and pickup tolerances are unused.
"""

from dataclasses import asdict
import json

from . import api_core as api
from . import ccu_core as ccu
from .native_capture import require, uint, validate_submission

SOURCE_COMMIT = "138ac18b6c496ca3faebab0fa8e8038757ec19a7"


def declarations(index, expected_order):
    entries = index["entries"]
    require(entries, "legacy capture has no executed entries")
    keys = [(e.get("entry_id"), e.get("batch")) for e in entries]
    require(
        keys == expected_order,
        "native capture execution order differs from successful result order",
    )
    iterations = {uint(e.get("iterations"), "iterations") for e in entries}
    warmups = {uint(e.get("warmup"), "warmup") for e in entries}
    require(
        len(iterations) == len(warmups) == 1,
        "legacy capture requires common warmup and iteration counts",
    )
    iterations, warmup = next(iter(iterations)), next(iter(warmups))
    require(iterations > 0, "legacy capture has no measured iterations")
    sequence = [("warmup", i) for i in range(warmup)] + [
        ("measured", i + 1) for i in range(iterations)
    ]
    for entry in entries:
        listed = entry.get("samples")
        require(isinstance(listed, list), "legacy entry.samples must be an array")
        require(
            [(s.get("phase"), s.get("iteration")) for s in listed] == sequence,
            "native capture warmup/measured sequence is missing, duplicated or reordered",
        )
        for descriptor in listed:
            validate_submission(
                descriptor.get("native_submission"),
                index["engine"],
                index.get("cycles_per_us"),
            )
    return entries, warmup, iterations


class LegacyCaptureReader:
    def __init__(self, device_reader):
        self.device_reader = device_reader
        self.audit = []
        self.cache = {}
        self.failures = {}

    def prepare(self, path, index, manifest, expected_order):
        if path in self.cache:
            return
        entries, warmup, iterations = declarations(index, expected_order)
        reader = self.device_reader
        profile = api.ProfileFile(
            path_type="api",
            device_id=manifest["global_rank"],
            json_path=path / "host" / "info.json",
            op_summary_path=None,
            operator=reader.operator,
            dtype=reader.dtype,
            raw_profile_root=path,
            local_device_id=index["collector_device_id"],
            node_key=str(manifest["node_index"]),
            sync_mode="mpi",
        )
        pid, _ = api.raw_profile_process_info(path)
        _, device = api.raw_profile_device_dir(path)
        require(
            pid == manifest["backend_pid"],
            "legacy PROF collector PID differs from rank",
        )
        require(
            device == index["collector_device_id"],
            "legacy PROF physical device differs from capture",
        )
        sizes = tuple(e["requested_bytes"] for e in entries)
        diagnostics = []
        if reader.engine == "aicpu_ts":
            task = api.ProfileParseTask(
                0, profile, sizes, warmup, iterations, reader.ranks
            )
            result = api.parse_raw_profile_task(task)
            samples, parse_audit = result.samples, result.audit
            diagnostics = result.diagnostics
            source = "legacy_raw_aicpu_order"
        else:
            traces = reader.exported_traces(path)
            # The old analyzer consumes each timeline once. Preserve that
            # assumption when a collector exports several files or slices.
            origins = {}
            for trace_index, exported in enumerate(traces):
                for event in exported.events:
                    identity = (
                        event.name,
                        event.pid,
                        event.tid,
                        str(event.start),
                        str(event.duration),
                        json.dumps(event.args, sort_keys=True),
                    )
                    previous = origins.setdefault(identity, trace_index)
                    require(
                        previous == trace_index,
                        "exported timelines repeat the same device event across files",
                    )
            events = sorted(
                (event for trace in traces for event in trace.events),
                key=lambda event: (event.start, event.end),
            )
            hardware = set().union(*(trace.hardware_pids for trace in traces))
            process_names = {}
            for trace in traces:
                process_names.update(trace.process_names)
            trace = api.ParsedTrace(
                events,
                [event.start for event in events],
                process_names,
                hardware,
                sum(t.complete_event_count for t in traces),
            )
            launchers = [
                e for e in events if ccu.is_api_launcher(trace, e, reader.operator)
            ]
            for event in launchers:
                require(
                    event.pid in hardware, "legacy CCU launcher is not a hardware event"
                )
                named = ccu.native_bench_operator(event.name)
                custom = ccu.CUSTOM_KERNEL_RE.fullmatch(event.name)
                direct = (
                    "mc2_"
                    + {
                        "AllReduce": "all_reduce",
                        "AllGather": "all_gather",
                        "ReduceScatter": "reduce_scatter",
                        "AllToAll": "all_to_all",
                        "AllToAllV": "all_to_all_v",
                        "AlltoAllvWrite": "all_to_all_v_write",
                    }[reader.operator]
                    + "_client"
                )
                require(
                    named == reader.operator
                    or (
                        custom is not None
                        and custom.group(1).lower() == reader.operator.lower()
                    )
                    or event.name.lower() == direct
                    or (
                        reader.operator == "AlltoAllvWrite"
                        and ccu.WRITE_BENCH_KERNEL_RE.fullmatch(event.name)
                    ),
                    "legacy CCU launcher operator differs from execution plan",
                )
            samples, _windows, parse_audit = ccu.parse_ccu_profile(
                profile,
                trace,
                reader.operator,
                reader.dtype,
                sizes,
                warmup,
                iterations,
                reader.ranks,
            )
            source = "legacy_ccu_launcher_order"
        record = dict(
            rank=manifest["global_rank"],
            source=source,
            source_commit=SOURCE_COMMIT,
            profile=str(path),
            entry_order=[list(k) for k in expected_order],
            sizes=list(sizes),
            warmup=warmup,
            iterations=iterations,
            parser_audit=asdict(parse_audit),
            diagnostics=diagnostics,
        )
        self.audit.append(record)
        require(
            parse_audit.status == "PASS" and len(samples) == len(entries) * iterations,
            "legacy capture sample count or phase parsing failed: "
            + str(asdict(parse_audit))
            + "; "
            + "; ".join(diagnostics[:5]),
        )
        mapped = {}
        for sample in samples:
            require(
                0 <= sample.size_index < len(entries),
                "legacy size_index is outside execution plan",
            )
            entry = entries[sample.size_index]
            require(
                sample.data_size == entry["requested_bytes"],
                "legacy data size differs from execution plan",
            )
            key = (entry["entry_id"], entry["batch"], sample.measured_iteration)
            require(key not in mapped, "duplicate legacy measured sample")
            # Raw interval identity is independent of client task and stream IDs.
            identity = (
                source,
                str(sample.kernel_start),
                str(sample.kernel_or_next_start),
            )
            mapped[key] = sample, identity
        self.cache[path] = mapped

    def sample(self, path, index, manifest, expected_order, key, iteration):
        if path in self.failures:
            raise ValueError(self.failures[path])
        try:
            self.prepare(path, index, manifest, expected_order)
        except Exception as exc:
            self.failures[path] = str(exc)
            raise
        result = self.cache[path].get((key[0], key[1], iteration))
        require(
            result is not None,
            "legacy parser did not produce the declared measured iteration",
        )
        # Preserve the legacy LoopGroup sum, including zero for an empty window.
        return result
