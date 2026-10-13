"""Retain behavioral rejection of altered IRQ-work drain contracts."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


LOOP = "\t\tdo {\n\t\t\t__irq_work_run();\n\t\t} while (!llist_empty(&__get_cpu_var(irq_work_list)));"
WRONG_CPU = (
    "\t\tif (WARN_ON_ONCE((long)hcpu != smp_processor_id()))\n\t\t\treturn NOTIFY_DONE;"
)
HARDIRQ = "\tBUG_ON(!in_irq());\n\t__irq_work_run();"
MUTATIONS = {
    "single-snapshot": (LOOP, "\t\t__irq_work_run();"),
    "frozen-notifier-skipped": (
        "(action & ~CPU_TASKS_FROZEN) == CPU_DYING",
        "action == CPU_DYING",
    ),
    "wrong-cpu-guard-removed": (
        WRONG_CPU,
        WRONG_CPU.replace("!= smp_processor_id()", "== smp_processor_id() + 4"),
    ),
    "hardirq-repeated-drain": (
        HARDIRQ,
        "\tBUG_ON(!in_irq());\n\tdo {\n\t\t__irq_work_run();\n\t} while (!llist_empty(&__get_cpu_var(irq_work_list)));",
    ),
    "inverted-empty-loop": (LOOP, LOOP.replace("!llist_empty", "llist_empty")),
    "hardirq-context-guard-bypassed": (
        "BUG_ON(!in_irq());",
        "BUG_ON(in_irq() && false);",
    ),
    "disabled-irqs-guard-bypassed": (
        "BUG_ON(!irqs_disabled());",
        "BUG_ON(irqs_disabled() && false);",
    ),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    source_path = arguments.source / "kernel/irq_work.c"
    if source_path.stat().st_size > 65536:
        raise ValueError("Mutation source exceeds bound")
    source = source_path.read_text()
    runner = Path(__file__).with_name("run.py")
    arguments.output.mkdir(parents=True, exist_ok=False)
    receipts = []
    for name, (old, new) in MUTATIONS.items():
        if source.count(old) != 1:
            raise ValueError("Mutation target is missing or ambiguous: " + name)
        directory = arguments.output / name
        (directory / "source/kernel").mkdir(parents=True)
        (directory / "source/kernel/irq_work.c").write_text(source.replace(old, new))
        for compiler in ("cc", "clang"):
            result = subprocess.run(
                [
                    sys.executable,
                    str(runner),
                    "--source",
                    str(directory / "source"),
                    "--output",
                    str(directory / compiler),
                    "--compiler",
                    compiler,
                ],
                capture_output=True,
                timeout=15,
                check=False,
            )
            if len(result.stdout) + len(result.stderr) > 65536:
                raise ValueError("Mutation diagnostics exceed bound")
            (directory / (compiler + ".stdout")).write_bytes(result.stdout)
            (directory / (compiler + ".stderr")).write_bytes(result.stderr)
            build = directory / compiler / "fixture-0.compile.log"
            if result.returncode == 0 or not build.exists() or build.read_bytes():
                raise ValueError(
                    "Behavioral mutation survives or compilation fails: " + name
                )
            if not (directory / compiler / "fixture-0").is_file():
                raise ValueError("Mutation lacks a successfully compiled executable")
            observed = [
                json.loads(line)
                for line in (directory / compiler / "fixture-0.run.jsonl")
                .read_text()
                .splitlines()
            ]
            timeout_rejection = b"TimeoutExpired" in result.stderr
            if name == "inverted-empty-loop":
                if not timeout_rejection:
                    raise ValueError(
                        "Infinite-loop mutation requires deadline rejection"
                    )
            elif len(observed) != 124 or not any(not row["passes"] for row in observed):
                raise ValueError("Mutation lacks complete failing runtime observations")
            receipts.append(
                {
                    "mutation": name,
                    "compiler": compiler,
                    "behavior_rejected": True,
                    "runner_exit": result.returncode,
                    "observations": len(observed),
                    "failing_observations": sum(not row["passes"] for row in observed),
                    "timeout_rejection": timeout_rejection,
                    "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
                    "mutated_source_sha256": hashlib.sha256(
                        (directory / "source/kernel/irq_work.c").read_bytes()
                    ).hexdigest(),
                    "runner_sha256": hashlib.sha256(runner.read_bytes()).hexdigest(),
                    "mutation_tool_sha256": hashlib.sha256(
                        Path(__file__).read_bytes()
                    ).hexdigest(),
                }
            )
    (arguments.output / "receipt.json").write_text(
        json.dumps(receipts, indent=2) + "\n"
    )
    print(json.dumps({"behavioral_mutations_rejected": len(receipts)}))


if __name__ == "__main__":
    main()
