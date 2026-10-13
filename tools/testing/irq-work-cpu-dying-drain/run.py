"""Falsify incomplete finite IRQ-work drainage using actual kernel functions."""

import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess

FUNCTIONS = (
    ("kernel/irq_work.c", "static bool irq_work_claim("),
    ("kernel/irq_work.c", "void __weak arch_irq_work_raise("),
    ("kernel/irq_work.c", "static void __irq_work_queue("),
    ("kernel/irq_work.c", "bool irq_work_queue("),
    ("kernel/irq_work.c", "static void __irq_work_run("),
    ("kernel/irq_work.c", "void irq_work_run("),
    ("kernel/irq_work.c", "static int irq_work_cpu_notify("),
    ("kernel/irq_work.c", "void irq_work_sync("),
)


def bounded_text(path, limit):
    if path.stat().st_size > limit:
        raise ValueError("Input or log exceeds bound: " + str(path))
    return path.read_text()


def digest(path):
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            hasher.update(chunk)
    return hasher.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", action="append")
    parser.add_argument("--expect-defect", action="store_true")
    parser.add_argument("--sanitize", action="store_true")
    arguments = parser.parse_args()
    fixture_root = Path(__file__).resolve().parent
    body = bounded_text(fixture_root / "prefix.c", 65536)
    constants = bounded_text(arguments.source / "kernel/irq_work.c", 65536)
    for name in ("IRQ_WORK_PENDING", "IRQ_WORK_BUSY", "IRQ_WORK_FLAGS"):
        matches = re.findall(r"^#define\s+" + name + r"\s+[^\n]+", constants, re.M)
        if len(matches) != 1:
            raise ValueError("Kernel IRQ-work constant is missing or ambiguous")
        body += matches[0] + "\n"
    receipts = []
    source_bytes = 0
    for filename, signature in FUNCTIONS:
        source = bounded_text(arguments.source / filename, 1024 * 1024)
        source_bytes += len(source.encode())
        if source_bytes > 2 * 1024 * 1024 or source.count(signature) != 1:
            raise ValueError("Source ceiling or unique function contract fails")
        begin = source.index(signature)
        end = source.index("\n}\n", begin) + 3
        function = source[begin:end]
        body += function
        receipts.append(
            {
                "source": filename,
                "signature": signature,
                "line": source.count("\n", 0, begin) + 1,
                "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
                "function_sha256": hashlib.sha256(function.encode()).hexdigest(),
            }
        )
    body += bounded_text(fixture_root / "cases.c", 65536)
    arguments.output.mkdir(parents=True, exist_ok=False)
    receipt_path = arguments.output / "receipt.json"
    report = {
        "status": "incomplete",
        "extracted_functions": receipts,
        "hardware_qualified": False,
        "source_bytes_read": source_bytes,
        "runs": [],
        "fixture_sources_sha256": {
            name: digest(fixture_root / name)
            for name in ("prefix.c", "cases.c", "run.py")
        },
        "generated_fixture_sha256": hashlib.sha256(body.encode()).hexdigest(),
        "boundary": "Exact production queue/run/notifier/sync functions and flag constants; CPU/IRQ predicates, preemption, lockless list and compare-exchange operations use explicit sequential host fixtures. Finite requeue completion excludes arbitrary infinite callbacks and concurrent/NMI producer closure.",
    }
    receipt_path.write_text(json.dumps(report, indent=2) + "\n")
    fixture = arguments.output / "fixture.c"
    fixture.write_text(body)
    for ordinal, compiler in enumerate(arguments.compiler or ["cc", "clang"]):
        compiler_path = shutil.which(compiler)
        if compiler_path is None:
            raise ValueError("Compiler executable is absent: " + compiler)
        compiler_path = Path(compiler_path).resolve()
        binary = arguments.output / ("fixture-" + str(ordinal))
        version_log = arguments.output / (binary.name + ".compiler-version.log")
        version_error = arguments.output / (binary.name + ".compiler-version.stderr")
        with version_log.open("wb") as stdout, version_error.open("wb") as stderr:
            version = subprocess.run(
                [str(compiler_path), "--version"],
                stdout=stdout,
                stderr=stderr,
                timeout=5,
                check=False,
            )
        version_text = bounded_text(version_log, 65536)
        if version.returncode or not version_text or bounded_text(version_error, 65536):
            raise ValueError("Compiler version probe rejects")
        command = [str(compiler_path), "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2"]
        if arguments.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        command += [str(fixture), "-o", str(binary)]
        compile_log = arguments.output / (binary.name + ".compile.log")
        with compile_log.open("wb") as stream:
            result = subprocess.run(
                command,
                stdout=stream,
                stderr=subprocess.STDOUT,
                timeout=60,
                check=False,
            )
        if result.returncode or bounded_text(compile_log, 65536):
            raise ValueError("Strict fixture compilation rejects: " + str(compile_log))
        run_log = arguments.output / (binary.name + ".run.jsonl")
        error_log = arguments.output / (binary.name + ".stderr")
        with run_log.open("wb") as stdout, error_log.open("wb") as stderr:
            result = subprocess.run(
                [str(binary)], stdout=stdout, stderr=stderr, timeout=5, check=False
            )
        if bounded_text(error_log, 65536):
            raise ValueError("Fixture stderr rejects: " + str(error_log))
        rows = [json.loads(line) for line in bounded_text(run_log, 65536).splitlines()]
        if len(rows) != 124:
            raise ValueError("Fixture census differs")
        identities = {
            (row["case"], row["cpu"], row["frozen"], row["nodes"], row["requeues"])
            for row in rows
        }
        if len(identities) != 124:
            raise ValueError("Fixture case identities are ambiguous")
        failing = [row for row in rows if not row["passes"]]
        if arguments.expect_defect:
            if (
                result.returncode != 1
                or len(failing) != 56
                or any(
                    row["case"] not in ("finite-requeue", "callback-chain")
                    or not row["preemption_balanced"]
                    or row["callbacks"] >= row["expected_callbacks"]
                    or row["pending_nodes"] == 0
                    for row in failing
                )
            ):
                raise ValueError("Baseline differs from finite requeue defect")
        elif result.returncode or failing:
            raise ValueError("Finite IRQ-work drain rejects: " + str(run_log))
        report["runs"].append(
            {
                "compiler": compiler,
                "compiler_executable": str(compiler_path),
                "compiler_sha256": digest(compiler_path),
                "compiler_version_sha256": digest(version_log),
                "compiler_version": version_text,
                "compile_command": command,
                "exit_code": result.returncode,
                "cases": len(rows),
                "failing_cases": len(failing),
                "sanitize": arguments.sanitize,
                "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            }
        )
        print(f"{compiler}: {len(rows)} cases, {len(failing)} incomplete drains")
    report["status"] = "defect-reproduced" if arguments.expect_defect else "passed"
    receipt_path.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
