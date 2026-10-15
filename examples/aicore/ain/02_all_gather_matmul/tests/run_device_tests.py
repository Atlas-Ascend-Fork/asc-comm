#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
"""Run bounded, full-output device checks; preserve each command and its log."""
import argparse
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--devices", required=True, help="at least two ACL device IDs in the same interconnect group")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--port", type=int, default=29650)
    args = parser.parse_args()
    devices = [int(x) for x in args.devices.split(",")]
    if len(devices) < 2 or len(set(devices)) != len(devices) or min(devices) < 0:
        parser.error("provide at least two unique nonnegative devices")
    if not args.binary.is_file():
        parser.error("binary does not exist")
    # Refuse to overwrite a previous verification report.
    args.output_dir.mkdir(parents=True, exist_ok=False)
    cases = [
        ("matmul_random", 2, 17, 80, 272, ["--mode", "matmul", "--random"]),
        ("get_random", 2, 257, 80, 272, ["--mode", "comm", "--tile-m", "256", "--random"]),
        ("fused_fp16_tail", 2, 257, 80, 272, ["--tile-m", "256", "--random"]),
        ("fused_bf16_tail", 2, 513, 144, 272, ["--dtype", "bf16", "--tile-m", "256", "--random"]),
        ("many_rounds", 2, 4353, 80, 272, ["--tile-m", "256"]),
        ("one_core", 2, 257, 80, 272, ["--cores", "1", "--tile-m", "256"]),
        ("delayed_peer", 2, 257, 80, 272, ["--tile-m", "256", "--delay-rank", "1", "--delay-ms", "20"]),
        ("hccl_cascade", 2, 257, 80, 272, ["--mode", "hccl", "--random"]),
    ]
    if len(devices) >= 4:
        cases.extend([
            ("four_rank_fp16", 4, 513, 144, 528, ["--tile-m", "256"]),
            ("four_rank_bf16", 4, 257, 80, 272, ["--dtype", "bf16", "--tile-m", "256", "--random"]),
            ("multiple_peers_per_core", 4, 257, 80, 272, ["--cores", "1", "--tile-m", "256"]),
        ])
    results = []
    for number, (name, ranks, m, k, n, extra) in enumerate(cases):
        command = [str(args.binary.resolve()), f"tcp://127.0.0.1:{args.port + number}",
                   str(ranks), str(m), str(k), str(n), "--devices", ",".join(map(str, devices[:ranks])),
                   "--warmup", "1", "--iters", "5", "--timeout", "180", *extra]
        print(name, " ".join(command), flush=True)
        log_path = args.output_dir / f"{name}.log"
        with log_path.open("w") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
        log_text = log_path.read_text()
        passed = result.returncode == 0 and "RESULT | Example=ain_all_gather_matmul | Status=PASS" in log_text
        results.append({"case": name, "command": command, "returncode": result.returncode,
                        "pass": passed, "log": str(log_path)})
        (args.output_dir / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        print(f"{name}: {'PASS' if passed else 'FAIL'}", flush=True)
        if not passed:
            print(log_text[-8000:], file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
