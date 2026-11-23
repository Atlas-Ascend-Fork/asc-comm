# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

# Reused from hccl_api_test 9ff8914c231dfb34a88865eded812d55fee38b8e (PR37).
# Original: perf_acceptance/ccu/scripts/analyze_kfc_markers.py.

"""KFC marker plugin: head/load statistics and strictly paired single executions."""

from collections import defaultdict
import json
import math
import sys

METRICS = ("kfc_Init", "kfc_LoadFuncParam")


def _number(value):
    value = float(value)
    if not math.isfinite(value) or value < 0:
        raise ValueError("invalid KFC timestamp/duration")
    return value


def _key(event):
    args = event["args"]
    stream = args.get("Physic Stream Id", args.get("Stream Id"))
    return (None if stream is None else int(stream), int(args["Task Id"]))


def _runs(instructions):
    runs, run = [], []
    for instruction in sorted(instructions):
        if run and instruction != run[-1] + 1:
            runs.append(run)
            run = []
        run.append(instruction)
    return runs + ([run] if run else [])


PROBE_GAPS = (
    (4, 3, 1, 1, 2, 3, 3),
    (4, 3, 1, 1, 3, 3, 2),
    (4, 3, 1, 2, 1, 3, 3),
    (4, 3, 1, 2, 3, 3, 1),
    (4, 3, 1, 3, 3, 1, 2),
    (4, 3, 1, 3, 3, 2, 1),
    (4, 3, 2, 1, 1, 3, 3),
)
PROBE_KINDS = {gaps: kind for kind, gaps in enumerate(PROBE_GAPS)}


def find_probes(waits):
    """Return recognized (kind, first Record start, last Wait end) tuples."""
    waits = sorted(waits, key=lambda wait: (wait[1], wait[0]))
    marks = []
    for index in range(len(waits) - 7):
        block = waits[index : index + 8]
        deltas = tuple(b[0] - a[0] for a, b in zip(block, block[1:]))
        kind = PROBE_KINDS.get(deltas)
        if kind is not None:
            marks.append((kind, block[0][1], block[-1][2]))
    return marks


def find_start_end(waits):
    """Return the outer bounds and inner duration for one pair on a single lane."""
    marks = [mark for mark in find_probes(waits) if mark[0] in (0, 1)]
    if len(marks) != 2 or [mark[0] for mark in marks] != [0, 1]:
        return None
    begin, finish = marks
    if begin[1] <= begin[2] <= finish[1] <= finish[2]:
        return begin[1], finish[2], finish[1] - begin[2]
    return None


def _boundary_tasks(events, mask_available=True):
    launches, waits = defaultdict(list), defaultdict(list)
    for event in events:
        if event.get("ph") != "X":
            continue
        if event.get("name") == "CCU_LAUNCH":
            launches[_key(event)].append(event)
        elif event.get("name") == "Wait" and (
            not mask_available or int(event.get("args", {}).get("Mask", 0)) == 2
        ):
            waits[_key(event)].append(event)
    rows = {}
    for key in launches.keys() | waits.keys():
        anchors = launches[key]
        if len(anchors) != 1:
            continue
        start = _number(anchors[0]["ts"])
        end = start + _number(anchors[0]["dur"])
        row = rows[key] = dict(
            start=start,
            end=end,
            launch=True,
            values={},
            extended=None,
            view="start_end",
        )
        lanes = defaultdict(list)
        for event in waits[key]:
            args = event["args"]
            lanes[
                (
                    event.get("pid"),
                    event.get("tid"),
                    args.get("Die Id"),
                    args.get("Mission Id"),
                )
            ].append(event)
        intervals = []
        for items in lanes.values():
            interval = find_start_end(
                (
                    int(e["args"]["Notify Instruction ID"]),
                    _number(e["ts"]),
                    _number(e["ts"]) + _number(e["dur"]),
                )
                for e in items
            )
            if interval is not None and start <= interval[0] and interval[1] <= end:
                intervals.append(interval[2])
        if len(intervals) == 1:
            row["values"]["kfc_ExecuteComm"] = row["extended"] = intervals[0]
    return rows


def _tasks(events):
    wait_args = [e.get("args", {}) for e in events if e.get("name") == "Wait"]
    if any(int(args.get("Mask", 0)) == 2 for args in wait_args):
        return _boundary_tasks(events)
    if wait_args and all("Mask" not in args for args in wait_args):
        # Older exports omit Mask. Require a complete pair before selecting this view.
        boundaries = _boundary_tasks(events, mask_available=False)
        if any(row["extended"] is not None for row in boundaries.values()):
            return boundaries
    waits, launches = defaultdict(list), defaultdict(list)
    for event in events:
        if event.get("ph") != "X":
            continue
        if event.get("name") == "CCU_LAUNCH":
            launches[_key(event)].append(event)
        elif event.get("name") == "Wait":
            args = event["args"]
            if (
                int(args.get("Notify Rank ID", 15)) == 15
                and int(args.get("Mask", 1)) == 1
            ):
                _number(event["dur"])
                waits[_key(event)].append(event)
    candidates = {
        key: [
            run
            for run in _runs(
                {
                    int(e["args"]["Notify Instruction ID"])
                    for e in items
                    if _number(e["dur"]) == 0
                }
            )
            if len(run) == 10
        ]
        for key, items in waits.items()
    }
    templates = {
        tuple(map(tuple, runs)) for runs in candidates.values() if len(runs) == 6
    }
    rows = {}
    for key in waits.keys() | launches.keys():
        items, launch = waits[key], launches[key]
        start = (
            _number(launch[0]["ts"]) if launch else min(_number(e["ts"]) for e in items)
        )
        end = start + _number(launch[0]["dur"]) if launch else None
        row = rows[key] = dict(
            start=start, end=end, launch=len(launch) == 1, values={}, extended=None
        )
        if len(launch) > 1:
            continue
        instructions = defaultdict(list)
        for event in items:
            instructions[int(event["args"]["Notify Instruction ID"])].append(
                _number(event["ts"])
            )
        runs = candidates.get(key, [])
        if len(runs) != 6:
            scores = {
                t: sum(all(i in instructions for i in run) for run in t)
                for t in templates
            }
            best = max(scores.values(), default=0)
            matches = [t for t, score in scores.items() if score == best and best >= 2]
            if len(matches) != 1:
                continue
            runs = matches[0]
        stamps = []
        for run in runs:
            counts = {len(instructions[i]) for i in run}
            count = counts.pop() if len(counts) == 1 else 0
            stamps.append(
                [
                    min(sorted(instructions[i])[visit] for i in run)
                    for visit in range(count)
                ]
                if count in (1, 2)
                else []
            )
        m1, m2, m3, m4, m5, m6 = stamps
        if len(m1) == len(m2) == 1 and m1[0] <= m2[0]:
            row["values"][METRICS[0]] = m2[0] - m1[0]
        if m3 and len(m3) == len(m4) and all(a <= b for a, b in zip(m3, m4)):
            row["values"][METRICS[1]] = m4[0] - m3[0]
        # M5 runs only for the normal message. A collapsed Finalize M3 occurs AFTER M5.
        if (
            m3
            and len(m5) == 1
            and end is not None
            and start <= m3[0] <= m5[0] <= end
            and (len(m3) == 1 or m5[0] <= m3[1] <= end)
        ):
            row["extended"] = m5[0] - m3[0]
    return rows


def _read(paths):
    tasks = {}
    for path in paths:
        with open(path) as file:
            data = json.load(file)
        events = data.get("traceEvents") if isinstance(data, dict) else data
        if not isinstance(events, list):
            raise ValueError(f"{path}: expected a timeline event list")
        for key, row in _tasks(events).items():
            if key in tasks and tasks[key] != row:
                raise ValueError(f"conflicting KFC marker data for task {key}")
            tasks[key] = row
    streams = defaultdict(list)
    for (stream, task), row in tasks.items():
        streams[stream].append(dict(row, stream=stream, task=task))
    return {
        stream: sorted(rows, key=lambda row: row["start"])
        for stream, rows in streams.items()
    }


def analyze(paths, measured_only=False, single_samples=None):
    streams = _read(paths)
    boundary = any(
        row.get("view") == "start_end" for rows in streams.values() for row in rows
    )
    metrics = ("kfc_ExecuteComm",) if boundary else METRICS
    view = "start_end" if boundary else "head_load"
    if single_samples is not None:
        for stream, rows in streams.items():
            # All 30 launch anchors must exist; otherwise ordinal matching could shift.
            if (
                stream is None
                or not measured_only
                or len(rows) != 30
                or not all(row["launch"] for row in rows)
                or any(
                    a["start"] >= b["start"] or a["end"] > b["start"]
                    for a, b in zip(rows, rows[1:])
                )
            ):
                print(
                    f"KFC stream {stream}: skipping unprovable sample mapping",
                    file=sys.stderr,
                )
                continue
            for sample in range(12, 17):
                row = rows[sample - 1]
                if row["extended"] is not None:
                    single_samples.append(
                        dict(
                            sample=sample,
                            stream=stream,
                            task=row["task"],
                            kfc_ExecuteComm_us=row["extended"],
                        )
                    )
                else:
                    print(
                        f"KFC stream {stream}: skipping sample {sample}, incomplete/ambiguous execution boundaries",
                        file=sys.stderr,
                    )
        return {}, view
    if not streams:
        raise ValueError("no KFC launches")
    values = defaultdict(list)
    for stream, rows in streams.items():
        if (not measured_only and len(rows) != 60) or (
            measured_only and len(rows) > 30
        ):
            raise ValueError(f"KFC stream {stream}: invalid launch count {len(rows)}")
        rows = rows[-30:]
        for metric in metrics:
            valid = [row["values"][metric] for row in rows if metric in row["values"]]
            if len(valid) < 15:
                raise ValueError(
                    f"KFC stream {stream}: only {len(valid)}/30 {metric} groups (less than half); rerun collection"
                )
            if len(valid) < 30:
                print(
                    f"KFC stream {stream}: skipping {30 - len(valid)}/30 {metric} groups",
                    file=sys.stderr,
                )
            values[metric].extend(valid)
    return values, view


# Protocol provenance for launch and analysis archives; matcher logic above is copied unchanged.
SOURCE_COMMIT = "9ff8914c231dfb34a88865eded812d55fee38b8e"
