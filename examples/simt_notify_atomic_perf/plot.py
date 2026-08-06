#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
"""Parse RESULT lines and plot submit-bandwidth curves."""

import argparse
from collections import defaultdict
from pathlib import Path
from statistics import mean


def parse_results(log_path: Path, operation: str):
    samples = defaultdict(lambda: defaultdict(list))
    with log_path.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if not line.startswith("RESULT |"):
                continue
            fields = {}
            for item in line.strip().split("|")[1:]:
                key, separator, value = item.strip().partition("=")
                if separator:
                    fields[key] = value
            interface = fields.get("接口")
            # The Notify comparison also includes WriteNbi as the lower-bound baseline.
            if interface != operation and not (operation == "notify" and interface == "write"):
                continue
            label = f'{fields["路径"]} {fields["接口"]}'
            size = int(fields["数据(B)"])
            samples[label][size].append(float(fields["Burst下发带宽(GB/s)"]))
    return samples


def main():
    parser = argparse.ArgumentParser(description="绘制下发带宽对比折线图")
    parser.add_argument("log", type=Path, help="run.sh生成的日志")
    parser.add_argument("--op", default="notify", help="要绘制的接口，默认notify")
    parser.add_argument("-o", "--output", type=Path, help="PNG输出路径")
    args = parser.parse_args()

    try:
        import matplotlib.pyplot as plt
    except ImportError as error:
        raise SystemExit("缺少matplotlib，请先安装后再画图") from error

    samples = parse_results(args.log, args.op)
    if not samples:
        raise SystemExit(f"日志中没有找到接口={args.op}的RESULT记录")

    output = args.output or args.log.with_suffix(".png")
    for label, points in sorted(samples.items()):
        sizes = sorted(points)
        bandwidth = [mean(points[size]) for size in sizes]
        plt.plot(sizes, bandwidth, marker="o", label=label)

    plt.xscale("log", base=2)
    plt.xlabel("Data size (B)")
    plt.ylabel("Submit bandwidth (GB/s)")
    plt.title(f"{args.op.upper()} submit bandwidth")
    plt.grid(True, which="both", linestyle="--", alpha=0.4)
    plt.legend()
    plt.tight_layout()
    plt.savefig(output, dpi=160)
    print(f"图已生成：{output}")


if __name__ == "__main__":
    main()
