# IRQ-work dying-CPU finite drainage

A callback can requeue itself after the runner clears IRQ_WORK_PENDING. The
CPU_DYING notifier detaches one list snapshot in the parent kernel and returns
with finite requeues still pending. ARM CPU disable stops the local timer;
stop-machine masks interrupts before calling the dying notifier. A future tick
therefore cannot finish those callback generations on the dying CPU.

The notifier drains successive local snapshots until the list becomes empty.
Ordinary irq_work_run retains one-snapshot hardirq batching. Wrong-CPU and
unrelated notifications retain their existing behavior. The change derives
from the source counterexample; an authoritative upstream repair donor remains
unidentified.

The fixture extracts eight actual queue, claim, run, notifier and sync functions
plus the three flag constants from kernel/irq_work.c. Host CPU/IRQ predicates,
list operations and compare-exchange operations model sequential execution.
The 124-case matrix covers four CPUs, empty/single/multiple lists, zero/one/
three/eight self-requeues, three-callback chains, frozen hotplug, duplicate
claims, wrong CPUs, unrelated actions, other-CPU ownership and hardirq batching.
Eight isolated child cases require SIGABRT from the actual hardirq and disabled
interrupt guards. Setup failures exit with distinct statuses; the guard callback
adds no independent context assertion. Core dumps stay disabled in the fixture.

Each initial work item with r finite self-requeues executes r+1 callbacks;
n independent initial items execute n*(r+1). The parent produces 56 failing
observations: 48 self-requeue cases and eight callback chains. Both compilers
pass all 124 candidate observations with ordinary and ASan/UBSan builds.
The seven behavioral mutations include both context-guard bypasses; every
mutation compiles successfully and rejects through observations or its deadline.

Set KERNEL_WORKTREE and OUTPUT_ROOT to the source checkout and a fresh output
root under the workbench trees/out directory. Run:

```sh
PYTHON=/usr/local/bin/python3
PYTHONDONTWRITEBYTECODE=1 "$PYTHON" \
  "$KERNEL_WORKTREE/tools/testing/irq-work-cpu-dying-drain/run.py" \
  --source "$KERNEL_WORKTREE" --output "$OUTPUT_ROOT/ordinary"
PYTHONDONTWRITEBYTECODE=1 "$PYTHON" \
  "$KERNEL_WORKTREE/tools/testing/irq-work-cpu-dying-drain/run.py" \
  --source "$KERNEL_WORKTREE" --output "$OUTPUT_ROOT/sanitizers" --sanitize
PYTHONDONTWRITEBYTECODE=1 "$PYTHON" \
  "$KERNEL_WORKTREE/tools/testing/irq-work-cpu-dying-drain/mutations.py" \
  --source "$KERNEL_WORKTREE" --output "$OUTPUT_ROOT/mutations"
```

Run the sanitizer command outside a ptraced sandbox; retain every sanitizer.
Use --expect-defect with the parent source to reproduce the 56 failures.
Source/function/template/compiler/version/binary hashes bind each receipt.
Each output directory must be new. GCC and Clang treat warnings as errors.

Finite sequential callback drainage does not prove concurrent/NMI producer
closure, ARM target execution, or termination for arbitrary infinite requeues.
An endlessly self-requeueing callback prevents fixed-point termination. Later
CPU_DYING notifiers can enqueue after this notifier returns; actual perf and
other producer quiescence require a separate target-source and hardware gate.
The serial target kernel build, final-image admission and attended RAM boot
remain prerequisites for hardware qualification and manifest promotion.
