#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

# Copied from hccl_api_test PR43, 3c699a80e8d8e2801f9c4d5baed99d92af2b5509.
# Original: scripts/ccu_marker_lanes.py.

"""识别 msprof JSON 中连续的 CCU Wait 插针；仅依赖标准库。

默认向 stdout 输出标记列表 JSON，不修改原始文件。
--mark-trace [路径] 切换为仅生成带标记泳道的 trace 副本，不输出标记列表；
省略路径时为 <输入名>.marked.json，可用 chrome://tracing 打开。
不使用 --mark-trace 时，-o 可另存标记列表。

默认 count=8、id-deltas=auto，与 devkit 探针对齐。
默认识别 kind=0（起点）、kind=1（终点）；旧版退出标记需 --kinds 0,1,2。
--mark-trace 默认按 stream 拆分 CCU 泳道，每条 marker 泳道紧邻对应 CCU stream 上方。
--no-split-streams 可保留原始 CCU 泳道和一条合并 marker 泳道。
--id-deltas none 为旧连续 ID 模式，kind 无法推断，标为 kind=?。
ts/dur 按 Chrome trace 的微秒解释，displayTimeUnit 不改变其单位。
输出区间从首条 Wait 的 ts 到最后一条 Wait 的 ts+dur；零耗时保持为零。
不同 stream/task/die/mission 分开处理，绝不根据 stream 推断 mission。
"""

import argparse
import json
import math
import sys
from collections import defaultdict
from decimal import Decimal
from pathlib import Path

KFC_SIGNATURES = (
    (4, 3, 1, 1, 2, 3, 3),
    (4, 3, 1, 1, 3, 3, 2),
    (4, 3, 1, 2, 1, 3, 3),
    (4, 3, 1, 2, 3, 3, 1),
    (4, 3, 1, 3, 3, 1, 2),
    (4, 3, 1, 3, 3, 2, 1),
    (4, 3, 2, 1, 1, 3, 3),
)
KFC_NAMES = (
    "algorithm_start",
    "algorithm_end",
    "queue_head_start",
    "queue_head_end",
    "parameter_load_start",
    "turn_end_signal_complete",
    "queue_end",
)


def num(value):
    result = Decimal(str(value))
    if not result.is_finite():
        raise ValueError("时间戳或时长不是有限数")
    return result


def convert(payload, opt):
    events = payload if isinstance(payload, list) else payload.get("traceEvents")
    if not isinstance(events, list):
        raise ValueError("输入应为事件数组，或含 traceEvents 数组的对象")
    ccu_pids = {
        str(e.get("pid"))
        for e in events
        if e.get("ph") == "M"
        and e.get("name") == "process_name"
        and "ccu" in str(e.get("args", {}).get("name", "")).lower()
    }
    if opt.ccu_pid:
        ccu_pids = set(opt.ccu_pid)
    if not ccu_pids:
        raise ValueError("未找到 CCU process_name；请用 --ccu-pid 指定 CCU 轨道的 pid")
    groups = defaultdict(list)
    fields = ("Physic Stream Id", "Task Id", "Die Id", "Mission Id")
    for index, event in enumerate(events):
        if (
            str(event.get("pid")) not in ccu_pids
            or event.get("name") != "Wait"
            or event.get("ph") != "X"
        ):
            continue
        args = dict(event.get("args", {}))
        if "Physic Stream Id" not in args and "Stream Id" in args:
            args["Physic Stream Id"] = args["Stream Id"]
        if "Notify Instruction ID" not in args:
            continue
        if (
            getattr(opt, "preset", "legacy") == "kfc7"
            and str(args.get("Mask", 2)) != "2"
        ):
            continue
        if opt.stream_id is not None and str(args.get(fields[0])) != opt.stream_id:
            continue
        if opt.task_id is not None and str(args.get(fields[1])) != opt.task_id:
            continue
        # 保留来源 tid，以免将原本已分轨的数据混合。
        key = (event.get("pid"), event.get("tid"), *(args.get(f) for f in fields))
        start = num(event["ts"])
        duration = num(event.get("dur", 0))
        if duration < 0:
            raise ValueError("Wait 的 dur 为负")
        groups[key].append(
            (start, int(args["Notify Instruction ID"]), index, start + duration)
        )

    deltas = getattr(opt, "id_deltas", None)

    def adjacent(left, right, delta=1):
        if right[1] != left[1] + delta:
            return False
        gap = right[0] - left[3]
        return opt.max_gap_us is None or gap <= num(opt.max_gap_us)

    # Keep marks inside the existing CCU process instead of creating a new
    # process whose small pid would place it at the top of the entire trace.
    marker_pid = next(iter(groups))[0] if groups else None
    used_tids = {str(e.get("tid")) for e in events if e.get("pid") == marker_pid}
    marker_tid = 0
    while str(marker_tid) in used_tids:
        marker_tid += 1
    thread_orders = [
        num(e.get("args", {}).get("sort_index", 0))
        for e in events
        if e.get("pid") == marker_pid
        and e.get("ph") == "M"
        and e.get("name") == "thread_sort_index"
    ]
    marker_order = min([Decimal(0), *thread_orders]) - 1
    added, removed = [], set()
    markers = []
    ambiguous = 0
    for key, waits in groups.items():
        waits.sort(key=lambda w: (w[0], w[1], w[2]))
        candidates = []
        if deltas or opt.start_id:
            for i, w in enumerate(waits):
                if opt.start_id and w[1] not in opt.start_id:
                    continue
                block = waits[i : i + opt.count]
                kinds = opt.kinds if deltas else (None,)
                for kind in kinds:
                    if getattr(opt, "preset", "legacy") == "kfc7":
                        steps = KFC_SIGNATURES[kind]
                    else:
                        steps = (
                            [
                                delta + (kind if j % 2 else 0)
                                for j, delta in enumerate(deltas)
                            ]
                            if deltas
                            else [1] * (opt.count - 1)
                        )
                    if len(block) == opt.count and all(
                        adjacent(a, b, d) for a, b, d in zip(block, block[1:], steps)
                    ):
                        candidates.append((block, kind))
        else:
            runs = []
            for w in waits:
                if not runs or not adjacent(runs[-1][-1], w):
                    runs.append([])
                runs[-1].append(w)
            candidates = [(r, None) for r in runs if len(r) == opt.count]
            ambiguous += sum(len(r) > opt.count for r in runs)
        if not candidates:
            continue
        pid, source_tid, stream, task, die, mission = key
        for block, kind in candidates:
            indices = {w[2] for w in block}
            if indices & removed:
                continue
            start, end = block[0][0], block[-1][3]
            marker_id = len(markers) + 1
            label = f"{opt.name} kind={kind if kind is not None else '?'} #{marker_id} [instr {block[0][1]}..{block[-1][1]}]"
            markers.append(
                {
                    "marker_id": marker_id,
                    "name": opt.name,
                    "kind": kind,
                    "pid": pid,
                    "source_tid": source_tid,
                    "stream_id": stream,
                    "task_id": task,
                    "die_id": die,
                    "mission_id": mission,
                    # 字符串保留大 epoch 时间的精度，单位在报告顶层明确标注。
                    "start_us": str(start),
                    "end_us": str(end),
                    "duration_us": str(end - start),
                    "wait_count": len(block),
                    "instruction_ids": [w[1] for w in block],
                    "source_event_indices": [w[2] for w in block],
                    "detection": "id_signature"
                    if deltas
                    else "explicit_id"
                    if opt.start_id
                    else "candidate",
                }
            )
            # 标准 JSON 数值，先用 Decimal 做差，避免大 epoch 时间相减丢精度。
            added.append(
                {
                    "name": label,
                    "cat": "CCU marker",
                    "ph": "X",
                    "pid": marker_pid,
                    "tid": marker_tid,
                    "ts": float(start),
                    "dur": float(end - start),
                    "args": {
                        **{f: v for f, v in zip(fields, key[2:]) if v is not None},
                        "Marker ID": marker_id,
                        "kind": kind,
                        "Source pid": pid,
                        "Source tid": source_tid,
                        "First instruction ID": block[0][1],
                        "Last instruction ID": block[-1][1],
                        "Wait count": len(block),
                        "Detection": "ID signature"
                        if deltas
                        else "explicit ID"
                        if opt.start_id
                        else "candidate",
                    },
                }
            )
            removed.update(indices)
    # Group iteration order is not chronological: #1 must be the earliest mark.
    ordered = sorted(zip(markers, added), key=lambda pair: num(pair[0]["start_us"]))
    markers = [marker for marker, _ in ordered]
    added = [event for _, event in ordered]
    for marker_id, (marker, event) in enumerate(ordered, 1):
        marker["marker_id"] = marker_id
        event["args"]["Marker ID"] = marker_id
        kind = marker["kind"] if marker["kind"] is not None else "?"
        ids = marker["instruction_ids"]
        event["name"] = (
            f"{opt.name} kind={kind} #{marker_id} [instr {ids[0]}..{ids[-1]}]"
        )
        if getattr(opt, "preset", "legacy") == "kfc7":
            marker["probe_name"] = KFC_NAMES[marker["kind"]]
            event["name"] += " " + marker["probe_name"]
            event["args"]["Probe name"] = marker["probe_name"]
    if markers:
        added[:0] = [
            {
                "ph": "M",
                "name": "thread_sort_index",
                "pid": marker_pid,
                "tid": marker_tid,
                "args": {"sort_index": float(marker_order)},
            },
            {
                "ph": "M",
                "name": "thread_name",
                "pid": marker_pid,
                "tid": marker_tid,
                "args": {"name": opt.name},
            },
        ]
    output = [
        e for i, e in enumerate(events) if not opt.remove_original or i not in removed
    ] + added
    if isinstance(payload, list):
        result = output
    else:
        result = {**payload, "traceEvents": output}
    issues = audit_markers(
        events, markers, kfc7=getattr(opt, "preset", "legacy") == "kfc7"
    )
    reasons_by_source = defaultdict(set)
    for issue in issues:
        reasons_by_source[issue["source"]].add(issue["reason"])
    for marker, event in zip(markers, (e for e in added if e.get("ph") == "X")):
        key = tuple(
            str(marker[k])
            for k in (
                "pid",
                "source_tid",
                "stream_id",
                "task_id",
                "die_id",
                "mission_id",
            )
        )
        reasons = sorted(reasons_by_source[key])
        status = "suspect_signature_candidate" if reasons else "signature_candidate"
        marker["audit_status"] = status
        marker["audit_reasons"] = reasons
        event["args"]["Audit status"] = status
        event["args"]["Audit reasons"] = reasons
        if reasons:
            event["name"] += " [suspect]"
    if opt.annotated_trace and opt.split_streams:
        original = [
            event
            for i, event in enumerate(events)
            if not opt.remove_original or i not in removed
        ]
        output = split_stream_lanes(
            original,
            [event for event in added if event.get("ph") == "X"],
            events,
            ccu_pids,
            opt.name,
        )
        result = (
            output if isinstance(payload, list) else {**payload, "traceEvents": output}
        )
    report = {
        "time_unit": "us",
        "marker_count": len(markers),
        "audit_issues": issues,
        "skipped_long_runs": ambiguous,
        "markers": markers,
    }
    return result, report


def split_stream_lanes(original, marks, source_events, ccu_pids, name):
    """Display each (source pid, stream) on paired marker/CCU lanes; do not change matching."""

    def stream_of(event):
        args = event.get("args", {})
        return args.get("Physic Stream Id", args.get("Stream Id"))

    keys = set()
    pids = {}
    for event in original:
        pid = event.get("pid")
        stream = stream_of(event)
        if str(pid) in ccu_pids and event.get("ph") != "M" and stream is not None:
            keys.add((str(pid), str(stream)))
            pids[str(pid)] = pid
    for event in marks:
        pid = event["args"]["Source pid"]
        stream = stream_of(event)
        keys.add((str(pid), str(stream) if stream is not None else "unknown"))
        pids[str(pid)] = pid

    used = defaultdict(set)
    orders = defaultdict(list)
    for event in source_events:
        pid = str(event.get("pid"))
        used[pid].add(str(event.get("tid")))
        if event.get("ph") == "M" and event.get("name") == "thread_sort_index":
            orders[pid].append(num(event.get("args", {}).get("sort_index", 0)))

    def allocate(pid):
        tid = 0
        while str(tid) in used[pid]:
            tid += 1
        used[pid].add(str(tid))
        return tid

    def stream_order(key):
        pid, stream = key
        return pid, (0, int(stream)) if stream.lstrip("-").isdigit() else (1, stream)

    lanes, metadata = {}, []
    by_pid = defaultdict(list)
    for key in sorted(keys, key=stream_order):
        by_pid[key[0]].append(key)
    for pid, pid_keys in by_pid.items():
        first_order = min([Decimal(0), *orders[pid]]) - 2 * len(pid_keys)
        for index, key in enumerate(pid_keys):
            marker_tid, ccu_tid = allocate(pid), allocate(pid)
            lanes[key] = (marker_tid, ccu_tid)
            for offset, tid, label in (
                (0, marker_tid, f"{name} / stream={key[1]}"),
                (1, ccu_tid, f"CCU / stream={key[1]}"),
            ):
                metadata.extend(
                    [
                        {
                            "ph": "M",
                            "name": "thread_name",
                            "pid": pids[pid],
                            "tid": tid,
                            "args": {"name": label},
                        },
                        {
                            "ph": "M",
                            "name": "thread_sort_index",
                            "pid": pids[pid],
                            "tid": tid,
                            "args": {
                                "sort_index": float(first_order + 2 * index + offset)
                            },
                        },
                    ]
                )
    output = []
    for event in original:
        stream = stream_of(event)
        key = (str(event.get("pid")), str(stream))
        if event.get("ph") != "M" and stream is not None and key in lanes:
            event = {
                **event,
                "tid": lanes[key][1],
                "args": {
                    **event.get("args", {}),
                    "Source pid": event.get("pid"),
                    "Source tid": event.get("tid"),
                },
            }
        output.append(event)
    for event in marks:
        stream = stream_of(event)
        key = (
            str(event["args"]["Source pid"]),
            str(stream) if stream is not None else "unknown",
        )
        output.append({**event, "pid": pids[key[0]], "tid": lanes[key][0]})
    return output + metadata


def audit_markers(events, markers, kfc7=False):
    """Report suspicious ordering without inventing missing probes or dropping evidence."""
    groups = defaultdict(list)
    for marker in markers:
        key = tuple(
            str(marker[k])
            for k in (
                "pid",
                "source_tid",
                "stream_id",
                "task_id",
                "die_id",
                "mission_id",
            )
        )
        groups[key].append(marker)
    loops = defaultdict(list)
    for event in events:
        if event.get("ph") != "X" or event.get("name") != "LoopGroup":
            continue
        args = event.get("args", {})
        key = tuple(
            str(value)
            for value in (
                event.get("pid"),
                event.get("tid"),
                args.get("Physic Stream Id", args.get("Stream Id")),
                args.get("Task Id"),
                args.get("Die Id"),
                args.get("Mission Id"),
            )
        )
        start = num(event["ts"])
        duration = num(event.get("dur", 0))
        if duration < 0:
            raise ValueError("LoopGroup 的 dur 为负")
        loops[key].append((start, start + duration))
    issues = []
    for key in sorted(groups.keys() | loops.keys()):
        rows = sorted(groups.get(key, []), key=lambda row: num(row["start_us"]))
        pending = None
        intervals = []
        for row in rows:
            kind = row["kind"]
            if kind == 0:
                if pending is not None:
                    issues.append(
                        {
                            "source": key,
                            "reason": "consecutive_start_candidates",
                            "marker_ids": [pending["marker_id"], row["marker_id"]],
                        }
                    )
                pending = row
            elif kind in ((1,) if kfc7 else (1, 2)):
                if pending is None:
                    issues.append(
                        {
                            "source": key,
                            "reason": "end_without_start",
                            "marker_id": row["marker_id"],
                        }
                    )
                elif num(row["start_us"]) < num(pending["end_us"]):
                    issues.append(
                        {
                            "source": key,
                            "reason": "overlapping_probe_boundaries",
                            "marker_ids": [pending["marker_id"], row["marker_id"]],
                        }
                    )
                elif kind == 1:
                    intervals.append((num(pending["end_us"]), num(row["start_us"])))
                pending = None
        if pending is not None:
            issues.append(
                {
                    "source": key,
                    "reason": "start_without_end",
                    "marker_id": pending["marker_id"],
                }
            )
        starts = [num(row["end_us"]) for row in rows if row["kind"] == 0]
        before = [
            start
            for start, _ in loops.get(key, [])
            if not starts or start < min(starts)
        ]
        if before:
            issues.append(
                {
                    "source": key,
                    "reason": "loopgroup_without_preceding_start",
                    "count": len(before),
                    "first_loopgroup_us": str(min(before)),
                }
            )
        uncovered = [
            (start, end)
            for start, end in loops.get(key, [])
            if not any(left <= start and end <= right for left, right in intervals)
            and start not in before
        ]
        if uncovered:
            issues.append(
                {
                    "source": key,
                    "reason": "loopgroup_outside_complete_probe_pair",
                    "count": len(uncovered),
                    "first_loopgroup_us": str(min(start for start, _ in uncovered)),
                }
            )
    return issues


def main():
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("input", type=Path)
    p.add_argument(
        "-o", "--output", type=Path, help="另存标记列表 JSON；默认仅输出到 stdout"
    )
    p.add_argument(
        "--mark-trace",
        dest="annotated_trace",
        type=Path,
        nargs="?",
        const=True,
        metavar="PATH",
        help="仅输出带标记泳道的 trace 副本，不输出分析 JSON；默认 <输入名>.marked.json",
    )
    p.add_argument(
        "--no-split-streams",
        dest="split_streams",
        action="store_false",
        help="关闭 --mark-trace 默认的按 stream 分泳道，保留原始 CCU 泳道和合并 marker 泳道",
    )
    p.add_argument(
        "--count",
        type=int,
        default=8,
        help="插针导出的 Wait 条数，默认 8（4 对），支持 4～32 偶数",
    )
    p.add_argument(
        "--preset",
        choices=("legacy", "kfc7"),
        default="legacy",
        help="kfc7：hccl_api_test 七类 KFC 探针；legacy：旧起止签名",
    )
    p.add_argument("--kinds", help="识别的 kind；legacy 默认 0,1，kfc7 默认 0～6")
    p.add_argument(
        "--start-id", type=int, action="append", help="插针首条指令 ID，可重复指定"
    )
    p.add_argument(
        "--id-deltas",
        default="auto",
        help="相邻 Wait 的 ID 差；默认 auto：count=12 用 1,2,1,4,1,3,1,6,1,5,1，"
        "count=8 用 1,2,1,4,1,3,1；自定义须有 count-1 项；none 使用连续 ID 模式",
    )
    p.add_argument(
        "--max-gap-us", type=float, help="相邻 Wait 的最大间隙，微秒；默认不限"
    )
    p.add_argument("--ccu-pid", action="append", help="显式指定 CCU pid，可重复")
    p.add_argument("--stream-id", help="仅处理指定 Physic Stream Id")
    p.add_argument("--task-id", help="仅处理指定 Task Id")
    p.add_argument("--name", default="CCU marker", help="新增区间和泳道名称")
    p.add_argument(
        "--remove-original", action="store_true", help="删除被合并的原始 Wait，默认保留"
    )
    opt = p.parse_args()
    if opt.preset == "kfc7" and (opt.count != 8 or opt.id_deltas != "auto"):
        p.error("--preset kfc7 使用固定的 --count 8 --id-deltas auto")
    if opt.kinds is None:
        opt.kinds = "0,1,2,3,4,5,6" if opt.preset == "kfc7" else "0,1"
    allowed_kinds = range(7) if opt.preset == "kfc7" else range(3)
    try:
        opt.kinds = tuple(int(value) for value in opt.kinds.split(","))
    except ValueError:
        p.error("--kinds 必须为逗号分隔的整数")
    if (
        not opt.kinds
        or len(set(opt.kinds)) != len(opt.kinds)
        or any(k not in allowed_kinds for k in opt.kinds)
    ):
        p.error("--kinds 超出所选 preset 的范围或存在重复")
    if opt.annotated_trace is True:
        opt.annotated_trace = opt.input.with_suffix(".marked.json")
    if opt.annotated_trace and opt.output:
        p.error("--mark-trace 与 -o 互斥；trace 路径直接传给 --mark-trace")
    if opt.count < 4 or opt.count > 32 or opt.count % 2:
        p.error("--count 必须为 4～32 的偶数")
    if opt.id_deltas == "auto":
        gaps = [
            2 if pair == 0 else pair + (3 if pair % 2 else 1)
            for pair in range(opt.count // 2 - 1)
        ]
        opt.id_deltas = [
            1 if i % 2 == 0 else gaps[i // 2] for i in range(opt.count - 1)
        ]
    elif opt.id_deltas == "none":
        opt.id_deltas = None
    else:
        try:
            opt.id_deltas = [int(x) for x in opt.id_deltas.split(",")]
        except ValueError:
            p.error("--id-deltas 必须是逗号分隔的整数")
        if len(opt.id_deltas) != opt.count - 1 or any(
            delta != 1 if i % 2 == 0 else not 2 <= delta <= 62
            for i, delta in enumerate(opt.id_deltas)
        ):
            p.error("--id-deltas 必须有 count-1 项；Record/Wait=1，跨对间隔=2..62")
    if opt.max_gap_us is not None and (
        not math.isfinite(opt.max_gap_us) or opt.max_gap_us < 0
    ):
        p.error("--max-gap-us 必须是非负有限数")
    paths = (
        [opt.input]
        + ([opt.output] if opt.output else [])
        + ([opt.annotated_trace] if opt.annotated_trace else [])
    )
    if len({path.resolve() for path in paths}) != len(paths):
        p.error("输入、标记列表、trace 副本必须使用不同路径")
    if opt.remove_original and not opt.annotated_trace:
        p.error("--remove-original 仅与 --mark-trace 一起使用")
    try:
        payload = json.loads(opt.input.read_text(encoding="utf-8-sig"))
        result, report = convert(payload, opt)
        report["source"] = str(opt.input.resolve())
        report_json = json.dumps(report, ensure_ascii=False, indent=2)
        if opt.output:
            opt.output.write_text(report_json + "\n", encoding="utf-8")
        if opt.annotated_trace:
            opt.annotated_trace.write_text(
                json.dumps(result, ensure_ascii=False, separators=(",", ":")),
                encoding="utf-8",
            )
    except (ValueError, TypeError, KeyError, OSError) as exc:
        p.exit(1, f"错误：{exc}\n")
    matches = report["marker_count"]
    if not opt.annotated_trace:
        print(report_json)
    print(
        f"识别 {matches} 个标记；跳过 {report['skipped_long_runs']} 个过长连续段。",
        file=sys.stderr,
    )
    if report["audit_issues"]:
        print(
            f"注意：发现 {len(report['audit_issues'])} 项配对/顺序异常；这些 mark 不能视为已确认的起止探针。",
            file=sys.stderr,
        )
        for issue in report["audit_issues"]:
            print(json.dumps(issue, ensure_ascii=False), file=sys.stderr)
    if opt.annotated_trace:
        print(f"标记泳道副本：{opt.annotated_trace}", file=sys.stderr)
    if opt.id_deltas:
        print(
            "注意：ID 差签名仍是候选；请对照当前版本最终指令 dump 校验。",
            file=sys.stderr,
        )
    elif not opt.start_id:
        print(
            "注意：自动匹配仅是候选；普通连续 Wait 也可能匹配。建议用 --start-id 限定。",
            file=sys.stderr,
        )
    return 0 if matches else 2


if __name__ == "__main__":
    sys.exit(main())
