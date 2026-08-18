#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

"""Convert RESULT log lines to DesignDocs section-10 style Markdown tables."""

import argparse
from collections import defaultdict
from pathlib import Path
from statistics import mean


REQUIRED_FIELDS = {
    "Path",
    "API",
    "CommitMode",
    "DataSize/B",
    "WqeCount",
    "IssueTime/us",
    "AverageIssue/ns",
    "CompletionBandwidth/GB/s",
    "DrainStatus",
    "Completed",
}


def parse_args():
    parser = argparse.ArgumentParser(
        description="Summarize WQE submission logs as Markdown tables."
    )
    parser.add_argument("logs", nargs="+", type=Path, help="RESULT log files")
    parser.add_argument(
        "--api",
        choices=("notify", "faa", "cas"),
        default="notify",
        help="API to summarize",
    )
    parser.add_argument(
        "--commit-mode",
        choices=("immediate", "last", "all"),
        default="all",
        help="Commit mode to summarize without mixing modes",
    )
    parser.add_argument(
        "--data-size",
        type=int,
        action="append",
        dest="data_sizes",
        help="Data size to include; may be specified more than once (default: 4096)",
    )
    parser.add_argument(
        "--all-sizes",
        action="store_true",
        help="Generate one table for every data size in the logs",
    )
    parser.add_argument(
        "-o", "--output", type=Path, help="Optional Markdown output file"
    )
    return parser.parse_args()


def parse_result_line(line):
    if not line.startswith("RESULT |"):
        return None
    fields = {}
    for item in line.split("|")[1:]:
        key, separator, value = item.strip().partition("=")
        if separator:
            fields[key] = value
    return fields if REQUIRED_FIELDS.issubset(fields) else None


def display_name(path):
    if path.startswith("SIMT-t"):
        return "simt t=" + path[len("SIMT-t") :]
    return path


def path_order(path):
    order = {
        "SIMT-t1": 0,
        "SIMT-t32": 1,
        "SIMT-t1024": 2,
    }
    return order.get(path, 100), path


def normalize_commit_mode(value):
    mode = value.lower()
    return mode if mode in ("immediate", "last") else None


def load_samples(logs, api, selected_commit_mode):
    samples = defaultdict(list)
    invalid = []
    for log in logs:
        for line_number, line in enumerate(
            log.read_text(encoding="utf-8", errors="replace").splitlines(), start=1
        ):
            fields = parse_result_line(line)
            if fields is None or fields["API"] != api:
                continue
            commit_mode = normalize_commit_mode(fields["CommitMode"])
            if commit_mode is None:
                invalid.append(
                    f"{log}:{line_number}: invalid CommitMode={fields['CommitMode']}"
                )
                continue
            if selected_commit_mode != "all" and commit_mode != selected_commit_mode:
                continue
            wqe_count = int(fields["WqeCount"])
            drain_status = int(fields["DrainStatus"])
            completed = int(fields["Completed"])
            if drain_status != 0 or completed != wqe_count:
                invalid.append(
                    f"{log}:{line_number}: Path={fields['Path']} "
                    f"CommitMode={commit_mode} DataSize/B={fields['DataSize/B']} DrainStatus={drain_status} "
                    f"Completed={completed}/{wqe_count}"
                )
                continue
            key = (fields["Path"], commit_mode, int(fields["DataSize/B"]))
            samples[key].append(
                {
                    "wqe_count": wqe_count,
                    "average_ns": float(fields["AverageIssue/ns"]),
                    "issue_us": (
                        float(fields["IssueTime/us"])
                        if "IssueTime/us" in fields
                        else None
                    ),
                    "completion_bandwidth": (
                        float(fields["CompletionBandwidth/GB/s"])
                        if "CompletionBandwidth/GB/s" in fields
                        else None
                    ),
                }
            )
    return samples, invalid


def build_markdown(samples, sizes):
    sections = []
    for size in sizes:
        rows = []
        for (path, commit_mode, data_size), values in samples.items():
            if data_size != size:
                continue
            issue_times = [
                item["issue_us"] for item in values if item["issue_us"] is not None
            ]
            completion_bandwidths = [
                item["completion_bandwidth"]
                for item in values
                if item["completion_bandwidth"] is not None
            ]
            rows.append(
                (
                    path,
                    commit_mode,
                    round(mean(item["wqe_count"] for item in values)),
                    mean(item["average_ns"] for item in values),
                    mean(issue_times) if issue_times else None,
                    mean(completion_bandwidths) if completion_bandwidths else None,
                )
            )
        if not rows:
            continue
        rows.sort(key=lambda row: (path_order(row[0]), row[1]))
        lines = [
            f"### 数据大小：{size}B",
            "",
            "| 路径 | 提交模式 | WQE总数 | 平均下发时延(ns) | 下发耗时(us) | 完成带宽(GB/s) |",
            "| :--- | :--- | ---: | ---: | ---: | ---: |",
        ]
        for (
            path,
            commit_mode,
            wqe_count,
            average_ns,
            issue_us,
            completion_bw,
        ) in rows:
            issue_us_text = "-" if issue_us is None else f"{issue_us:.3f}"
            completion_bw_text = (
                "-" if completion_bw is None else f"{completion_bw:.3f}"
            )
            lines.append(
                f"| {display_name(path)} | {commit_mode.capitalize()} | {wqe_count} | {average_ns:.3f} | "
                f"{issue_us_text} | {completion_bw_text} |"
            )
        sections.append("\n".join(lines))
    return "\n\n".join(sections) + ("\n" if sections else "")


def main():
    args = parse_args()
    samples, invalid = load_samples(args.logs, args.api, args.commit_mode)
    if invalid:
        print("以下失败样本未计入汇总：")
        for item in invalid:
            print(f"- {item}")
        print()
    available_sizes = sorted({size for _, _, size in samples})
    default_size = 4096 if args.api == "notify" else 8
    sizes = available_sizes if args.all_sizes else (args.data_sizes or [default_size])
    markdown = build_markdown(samples, sizes)
    if not markdown:
        raise SystemExit("No valid RESULT data matched the selected API and data size.")
    print(markdown, end="")
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(markdown, encoding="utf-8")
        print(f"已生成：{args.output}")


if __name__ == "__main__":
    main()
