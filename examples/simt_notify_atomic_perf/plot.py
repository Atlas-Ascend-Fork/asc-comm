#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

"""Parse RESULT logs and generate dependency-free CSV and SVG curves."""

import argparse
import csv
import html
import math
import sys
from collections import defaultdict
from pathlib import Path
from statistics import mean


CSV_FIELDS = [
    "Path",
    "API",
    "CommitMode",
    "DataSize/B",
    "WqeCount",
    "AverageIssue/ns",
    "IssueTime/us",
    "CompletionTime/us",
    "CompletionBandwidth/GB/s",
]
REQUIRED_FIELDS = set(CSV_FIELDS) | {"DrainStatus", "Completed"}
COLORS = ["#3977d5", "#22a06b", "#e27830", "#8f5bd6", "#d64562", "#008c95"]


def parse_args():
    parser = argparse.ArgumentParser(
        description="Plot WQE submission results without matplotlib."
    )
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--api", choices=("notify", "faa", "cas"), default="notify")
    parser.add_argument(
        "--commit-mode",
        choices=("immediate", "last", "all"),
        default="all",
    )
    parser.add_argument(
        "--metric",
        choices=("bandwidth", "latency"),
        default="bandwidth",
    )
    parser.add_argument("-o", "--output", type=Path)
    parser.add_argument("--csv", dest="csv_output", type=Path)
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


def normalize_commit_mode(value):
    mode = value.lower()
    return mode if mode in ("immediate", "last") else None


def load_rows(paths, api, selected_commit_mode):
    samples = defaultdict(list)
    invalid = []
    for path in paths:
        for line_number, line in enumerate(
            path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1
        ):
            fields = parse_result_line(line)
            if fields is None or fields["API"] != api:
                continue
            commit_mode = normalize_commit_mode(fields["CommitMode"])
            if commit_mode is None:
                invalid.append(
                    f"{path}:{line_number}: invalid CommitMode={fields['CommitMode']}"
                )
                continue
            if selected_commit_mode != "all" and commit_mode != selected_commit_mode:
                continue
            wqe_count = int(fields["WqeCount"])
            drain_status = int(fields["DrainStatus"])
            completed = int(fields["Completed"])
            if drain_status != 0 or completed != wqe_count:
                invalid.append(
                    f"{path}:{line_number}: Path={fields['Path']} CommitMode={commit_mode} "
                    f"DrainStatus={drain_status} Completed={completed}/{wqe_count}"
                )
                continue
            key = (
                fields["Path"],
                fields["API"],
                commit_mode,
                int(fields["DataSize/B"]),
            )
            samples[key].append(fields)

    rows = []
    for (path, operation, commit_mode, size), values in sorted(
        samples.items(), key=lambda item: (item[0][3], item[0][0], item[0][2])
    ):
        series = f"{path}-{commit_mode.capitalize()}"
        issue_times = [
            float(value["IssueTime/us"]) for value in values if "IssueTime/us" in value
        ]
        completion_times = [
            float(value["CompletionTime/us"])
            for value in values
            if "CompletionTime/us" in value
        ]
        completion_bandwidths = [
            float(value["CompletionBandwidth/GB/s"])
            for value in values
            if "CompletionBandwidth/GB/s" in value
        ]
        rows.append(
            {
                "Path": path,
                "API": operation,
                "CommitMode": commit_mode.capitalize(),
                "_Series": series,
                "DataSize/B": size,
                "WqeCount": round(mean(int(value["WqeCount"]) for value in values)),
                "AverageIssue/ns": mean(
                    float(value["AverageIssue/ns"]) for value in values
                ),
                "IssueTime/us": mean(issue_times) if issue_times else None,
                "CompletionTime/us": mean(completion_times)
                if completion_times
                else None,
                "CompletionBandwidth/GB/s": mean(completion_bandwidths)
                if completion_bandwidths
                else None,
            }
        )
    return rows, invalid


def write_csv(rows, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=CSV_FIELDS, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def metric_info(metric):
    if metric == "latency":
        return "AverageIssue/ns", "Average issue latency (ns)", "WQE issue latency"
    return (
        "CompletionBandwidth/GB/s",
        "Completion bandwidth (GB/s)",
        "WQE completion bandwidth",
    )


def write_svg(rows, output, metric, api):
    width, height = 940, 560
    left, right, top, bottom = 105, 45, 75, 95
    plot_width = width - left - right
    plot_height = height - top - bottom
    value_key, ylabel, title = metric_info(metric)
    title = f"{api.upper()} {title}"

    metric_rows = [row for row in rows if row.get(value_key) is not None]
    if not metric_rows:
        raise SystemExit(f"No {value_key} data found in the selected logs.")
    sizes = sorted({int(row["DataSize/B"]) for row in metric_rows})
    paths = sorted({row["_Series"] for row in metric_rows})
    maximum = max(float(row[value_key]) for row in metric_rows)
    if maximum <= 0:
        raise SystemExit(f"No positive {value_key} data found.")
    maximum *= 1.15

    min_log = math.log2(sizes[0])
    max_log = math.log2(sizes[-1])

    def x_position(size):
        if min_log == max_log:
            return left + plot_width / 2
        return left + plot_width * (math.log2(size) - min_log) / (max_log - min_log)

    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        f'<text x="{width / 2}" y="34" text-anchor="middle" font-family="sans-serif" font-size="21">{html.escape(title)}</text>',
        f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_height}" stroke="#333"/>',
        f'<line x1="{left}" y1="{top + plot_height}" x2="{left + plot_width}" y2="{top + plot_height}" stroke="#333"/>',
        f'<text x="25" y="{top + plot_height / 2}" transform="rotate(-90 25 {top + plot_height / 2})" text-anchor="middle" font-family="sans-serif" font-size="14">{html.escape(ylabel)}</text>',
        f'<text x="{left + plot_width / 2}" y="{height - 24}" text-anchor="middle" font-family="sans-serif" font-size="14">Data size (B, log2 scale)</text>',
    ]

    for tick in range(6):
        value = maximum * tick / 5
        y = top + plot_height - plot_height * tick / 5
        lines.append(
            f'<line x1="{left}" y1="{y:.1f}" x2="{left + plot_width}" y2="{y:.1f}" stroke="#ddd"/>'
        )
        lines.append(
            f'<text x="{left - 10}" y="{y + 5:.1f}" text-anchor="end" font-family="sans-serif" font-size="12">{value:.3f}</text>'
        )

    for size in sizes:
        x = x_position(size)
        lines.append(
            f'<line x1="{x:.1f}" y1="{top + plot_height}" x2="{x:.1f}" y2="{top + plot_height + 5}" stroke="#333"/>'
        )
        lines.append(
            f'<text x="{x:.1f}" y="{top + plot_height + 24}" text-anchor="middle" font-family="sans-serif" font-size="11">{size}</text>'
        )

    for path_index, path in enumerate(paths):
        color = COLORS[path_index % len(COLORS)]
        path_rows = sorted(
            (row for row in metric_rows if row["_Series"] == path),
            key=lambda row: row["DataSize/B"],
        )
        points = []
        for row in path_rows:
            x = x_position(int(row["DataSize/B"]))
            if len(sizes) == 1:
                x += (path_index - (len(paths) - 1) / 2) * 26
            value = float(row[value_key])
            y = top + plot_height * (1 - value / maximum)
            points.append((x, y, value))
        if len(points) > 1:
            coordinates = " ".join(f"{x:.1f},{y:.1f}" for x, y, _ in points)
            lines.append(
                f'<polyline points="{coordinates}" fill="none" stroke="{color}" stroke-width="2"/>'
            )
        for x, y, value in points:
            lines.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.5" fill="{color}"/>')
            lines.append(f"<title>{html.escape(path)}: {value:.6f}</title>")
        legend_x = left + path_index * 145
        lines.append(
            f'<line x1="{legend_x}" y1="52" x2="{legend_x + 18}" y2="52" stroke="{color}" stroke-width="3"/>'
        )
        lines.append(
            f'<text x="{legend_x + 24}" y="57" font-family="sans-serif" font-size="12">{html.escape(path)}</text>'
        )

    lines.append("</svg>")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    args = parse_args()
    rows, invalid = load_rows(args.logs, args.api, args.commit_mode)
    for item in invalid:
        print(f"ignored invalid sample: {item}", file=sys.stderr)
    if not rows:
        raise SystemExit(
            f"No valid RESULT line found for API={args.api}, CommitMode={args.commit_mode}."
        )
    output = args.output or Path(f"{args.api}_{args.commit_mode}_{args.metric}.svg")
    csv_output = args.csv_output or output.with_suffix(".csv")
    write_csv(rows, csv_output)
    write_svg(rows, output, args.metric, args.api)
    print(f"generated: {csv_output}")
    print(f"generated: {output}")


if __name__ == "__main__":
    main()
