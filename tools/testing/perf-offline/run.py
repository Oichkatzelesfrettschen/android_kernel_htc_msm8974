"""Compile production perf lifetime functions against bounded host models."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

FUNCTIONS = {
    "core": [
        ("kernel/events/core.c", signature)
        for signature in (
            "static void\nlist_del_event(",
            "static void __ref perf_remove_from_context(",
            "int perf_event_release_kernel(",
            "static void _perf_event_disable(",
            "static void _perf_event_enable(",
            "static void perf_event_quiesce_cpu(",
            "static void perf_event_resume_cpu(",
        )
    ],
    "arm": [
        (
            "arch/arm/kernel/perf_event_msm_krait.c",
            "static int msm_test_set_ev_constraint(",
        ),
        (
            "arch/arm/kernel/perf_event_msm_krait.c",
            "static int msm_clear_ev_constraint(",
        ),
        (
            "arch/arm/mach-msm/perf_event_msm_krait_l2.c",
            "static int msm_l2_test_set_ev_constraint(",
        ),
        (
            "arch/arm/mach-msm/perf_event_msm_krait_l2.c",
            "static int msm_l2_clear_ev_constraint(",
        ),
        ("arch/arm/kernel/perf_event.c", "static void\narmpmu_del("),
        ("arch/arm/kernel/perf_event.c", "static int\narmpmu_add("),
    ],
    "irq": [("arch/arm/kernel/perf_event.c", "static int __cpuinit pmu_cpu_notify(")],
    "irqwork": [
        ("kernel/irq_work.c", signature)
        for signature in (
            "static void __irq_work_run(",
            "void irq_work_run(",
            "static int irq_work_cpu_notify(",
            "void irq_work_sync(",
        )
    ],
}


def extract(source: str, signature: str) -> tuple[str, int]:
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end], source.count("\n", 0, start) + 1


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source", type=Path, default=Path(__file__).resolve().parents[3]
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", action="append")
    parser.add_argument("--sanitize", action="store_true")
    arguments = parser.parse_args()
    arguments.output.mkdir(parents=True, exist_ok=True)
    fixture_root = Path(__file__).resolve().parent
    receipts = []
    receipt_path = arguments.output / "receipt.json"
    receipt_path.write_text(json.dumps({"status": "incomplete"}) + "\n")
    for name, requests in FUNCTIONS.items():
        body = (fixture_root / (name + "-prefix.c")).read_text()
        if name == "arm":
            header = (
                arguments.source
                / "arch/arm/mach-msm/include/mach/msm-krait-l2-accessors.h"
            )
            constants = header.read_text()
            for constant in (
                "L2_EVT_MASK",
                "L2_TRACECTR_PREFIX",
                "L2CYCLE_CTR_RAW_CODE",
                "EVENT_PREFIX_MASK",
                "EVENT_PREFIX_SHIFT",
            ):
                match = re.search(
                    r"^#define\s+" + constant + r"\s+[^\n]+", constants, re.MULTILINE
                )
                if match is None:
                    raise ValueError("Missing constant: " + constant)
                body += match.group() + "\n"
        for filename, signature in requests:
            source = (arguments.source / filename).read_text()
            function, line = extract(source, signature)
            body += function
            receipts.append(
                {
                    "fixture": name,
                    "source": filename,
                    "line": line,
                    "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
                    "signature": signature,
                    "function_sha256": hashlib.sha256(function.encode()).hexdigest(),
                }
            )
        body += (fixture_root / (name + "-cases.c")).read_text()
        fixture = arguments.output / (name + ".c")
        fixture.write_text(body)
        for ordinal, compiler in enumerate(arguments.compiler or ["cc", "clang"]):
            binary = arguments.output / (name + "-" + str(ordinal))
            command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2"]
            if arguments.sanitize:
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            command += [str(fixture), "-o", str(binary)]
            subprocess.run(command, check=True)
            result = subprocess.run(
                [str(binary)], check=False, capture_output=True, text=True
            )
            (arguments.output / (binary.name + ".log")).write_text(
                result.stdout + result.stderr
            )
            result.check_returncode()
            print(compiler + ": " + result.stdout.strip())
            receipts.append(
                {
                    "fixture": name,
                    "compile_command": command,
                    "run_command": [str(binary)],
                    "exit_code": result.returncode,
                }
            )
    receipt_path.write_text(
        json.dumps({"status": "passed", "checks": receipts}, indent=2) + "\n"
    )


if __name__ == "__main__":
    main()
