#!/usr/bin/env python3
"""Convert RESULT log lines to DesignDocs section-10 style Markdown tables."""

import argparse
from collections import defaultdict
from pathlib import Path
from statistics import mean


REQUIRED_FIELDS = {
    "Path",
    "API",
    "DataSize/B",
    "WqeCount",
    "DoorbellCount",
    "TotalTime/us",
    "PacketRate/Mpps",
    "DrainStatus",
    "Completed",
}


def parse_args():
    parser = argparse.ArgumentParser(
        description="Summarize WQE submission logs as Markdown tables."
    )
    parser.add_argument("logs", nargs="+", type=Path, help="RESULT log files")
    parser.add_argument("--api", default="notify", help="API to summarize")
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
    parser.add_argument("-o", "--output", type=Path, help="Optional Markdown output file")
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
        return "simt t=" + path[len("SIMT-t"):]
    if path == "AIV-MTE3":
        return "mte3"
    return path


def path_order(path):
    order = {
        "scalar": 0,
        "AIV-MTE3": 1,
        "SIMT-t1": 2,
        "SIMT-t32": 3,
        "SIMT-t1024": 4,
    }
    return order.get(path, 100), path


def load_samples(logs, api):
    samples = defaultdict(list)
    invalid = []
    for log in logs:
        for line_number, line in enumerate(
            log.read_text(encoding="utf-8", errors="replace").splitlines(), start=1
        ):
            fields = parse_result_line(line)
            if fields is None or fields["API"] != api:
                continue
            wqe_count = int(fields["WqeCount"])
            drain_status = int(fields["DrainStatus"])
            completed = int(fields["Completed"])
            if drain_status != 0 or completed != wqe_count:
                invalid.append(
                    f"{log}:{line_number}: Path={fields['Path']} "
                    f"DataSize/B={fields['DataSize/B']} DrainStatus={drain_status} "
                    f"Completed={completed}/{wqe_count}"
                )
                continue
            key = (fields["Path"], int(fields["DataSize/B"]))
            samples[key].append(
                {
                    "wqe_count": wqe_count,
                    "doorbell_count": int(fields["DoorbellCount"]),
                    "total_us": float(fields["TotalTime/us"]),
                    "packet_rate": float(fields["PacketRate/Mpps"]),
                    "completion_us": (
                        float(fields["CompletionTime/us"])
                        if "CompletionTime/us" in fields else None
                    ),
                    "completion_bandwidth": (
                        float(fields["CompletionBandwidth/GB/s"])
                        if "CompletionBandwidth/GB/s" in fields else None
                    ),
                }
            )
    return samples, invalid


def build_markdown(samples, sizes):
    sections = []
    for size in sizes:
        rows = []
        for (path, data_size), values in samples.items():
            if data_size != size:
                continue
            completion_times = [
                item["completion_us"] for item in values if item["completion_us"] is not None
            ]
            completion_bandwidths = [
                item["completion_bandwidth"]
                for item in values
                if item["completion_bandwidth"] is not None
            ]
            rows.append(
                (
                    path,
                    round(mean(item["wqe_count"] for item in values)),
                    round(mean(item["doorbell_count"] for item in values)),
                    mean(item["total_us"] for item in values),
                    mean(item["packet_rate"] for item in values),
                    mean(completion_times) if completion_times else None,
                    mean(completion_bandwidths) if completion_bandwidths else None,
                )
            )
        if not rows:
            continue
        rows.sort(key=lambda row: path_order(row[0]))
        lines = [
            f"### 数据大小：{size}B",
            "",
            "| 填写WQE的方式 | WQE总数 | 敲DB次数 | 下发耗时(us) | 包率(Mpps) | 完成耗时(us) | 完成带宽(GB/s) |",
            "| :--- | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
        for path, wqe_count, doorbell_count, total_us, packet_rate, completion_us, completion_bw in rows:
            completion_us_text = "-" if completion_us is None else f"{completion_us:.3f}"
            completion_bw_text = "-" if completion_bw is None else f"{completion_bw:.3f}"
            lines.append(
                f"| {display_name(path)} | {wqe_count} | {doorbell_count} | "
                f"{total_us:.3f} | {packet_rate:.3f} | {completion_us_text} | {completion_bw_text} |"
            )
        sections.append("\n".join(lines))
    return "\n\n".join(sections) + ("\n" if sections else "")


def main():
    args = parse_args()
    samples, invalid = load_samples(args.logs, args.api)
    if invalid:
        print("以下失败样本未计入汇总：")
        for item in invalid:
            print(f"- {item}")
        print()
    available_sizes = sorted({size for _, size in samples})
    sizes = available_sizes if args.all_sizes else (args.data_sizes or [4096])
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
