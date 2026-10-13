# Perf event lifetime host checks

The runner extracts production functions byte-for-byte and compiles them
against explicit models for callback delivery, context locks, PMU registers,
IRQs, and pending-work lists. The models expose ownership transitions and
assert the target CPU, remaining references, allocation masks, constraints,
and IRQ enable state.

Run from the kernel checkout with an output directory under the workbench's
`trees/out` directory:

```sh
PYTHON=$(command -v python3)
"$PYTHON" tools/testing/perf-offline/run.py --output /absolute/trees/out/perf-host
"$PYTHON" tools/testing/perf-offline/run.py --sanitize --output /absolute/trees/out/perf-host-sanitize
```

The runner accepts `--source` for an explicit kernel checkout and repeated
`--compiler` arguments. GCC and Clang are the default pair. Each compile uses
`-Wall -Wextra -Werror`. The receipt pins extracted source hashes, functions,
compile commands, run commands, and exit status. A failed compile or assertion
returns failure before a success receipt is written.

The core cases exercise online and offline close, twenty retained restarts,
offline enable/disable, error-state enable, target cgroup cleanup and unique
shared-context traversal. Scheduling and hardware deletion inside the core
model are explicit stubs; the ARM fixture separately exercises the real
add/delete and constraint functions. The ARM cases cover repeated L2 code
sharing, duplicate rejection, slot-allocation rollback, the cycle-counter
constraint exemption and Krait's upper 32-bit constraint bank. The IRQ cases
cover empty-slot PPI ownership, retained IRQ allocation, failed-down rollback,
frozen hotplug and startup after IRQ release. IRQ-work cases drain pending
owners during frozen CPU_DYING and preserve the ordinary hardirq runner.

The fixture models do not establish kernel lockdep behavior, complete task
migration interleavings, hardware register correctness, timing accuracy,
interrupt delivery or sustained performance. ARM object compilation, a full
kernel build and attended device lifecycles remain independent acceptance
gates. Preserve thermal CPU-up vetoes during device close tests. Verify retained
counts and sampling after repeated online/offline cycles, including grouped
hardware/software events and L2 constraints.
