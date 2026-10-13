# Perf software-event preemption balance

Recursion rejection suppresses a nested software event while retaining the
caller's execution context. `__perf_sw_event()` disables preemption before
acquiring a recursion slot, so every exit also restores the caller's preemption
nesting. A failed acquisition leaves the slot under its original owner.

The host fixture extracts five production functions from `kernel/events/core.c`
and `kernel/events/internal.h`: recursion acquisition/release, the two per-CPU
wrappers and `__perf_sw_event()`. The extraction records whole-file and function
hashes, source lines, compiler commands, executable hashes and results. The
fixture supplies explicit context predicates, preemption counters and a callback
that injects one nested event. The barrier hook records successful slot acquisition
and release and rejects a negative slot before access. Those substitutions
delimit the host proof. Each receipt also hashes the fixture templates, runner,
generated C, resolved compiler executable and version output.

All 32 cases check entry/exit preemption depth, matching disable/enable counts,
recursion ownership, acquisition/release counts, sample initialization and
callback counts. Four context
slots each exercise caller nesting depths 0 and 5, ordinary sampling, an occupied
slot, nested same-slot rejection and cross-slot independence. Cross-slot cases
exercise accounting; the fixture does not model physical interrupt priority.

The unrepaired function fails exactly 16 cases: occupied-slot rejection and
nested same-slot rejection each leave one extra preemption-disable operation.
The corrected function routes rejection to the shared preemption-enable exit and
releases the recursion slot only after successful acquisition. Source history
attributes the preemption pair to upstream
`1c024eca51fdc965290acf342ae16a476c2189d0` and the older rejection return to
`4ed7c92d68a5387ba5f7030dc76eab03558e27f5`. The common-exit repair derives from
the observed control flow; its provenance remains separate from an upstream
repair commit that the bounded local history search leaves unidentified.

Run from the Android workbench with `KERNEL_TREE` selecting the intended source:

```sh
PYTHON=/usr/local/bin/python3
PYTHONDONTWRITEBYTECODE=1 "$PYTHON" \
  "$KERNEL_TREE/tools/testing/perf-software-event-preemption-balance/run.py" \
  --source "$KERNEL_TREE" \
  --output "$ANDROID_ROOT/trees/out/perf-software-event-preemption-balance/fixed-host"
PYTHONDONTWRITEBYTECODE=1 "$PYTHON" \
  "$KERNEL_TREE/tools/testing/perf-software-event-preemption-balance/run.py" \
  --source "$KERNEL_TREE" --sanitize \
  --output "$ANDROID_ROOT/trees/out/perf-software-event-preemption-balance/fixed-sanitized"
```

Each output directory is new. GCC and Clang compile under
`-std=c11 -Wall -Wextra -Werror -O2`; sanitized runs add AddressSanitizer and
UndefinedBehaviorSanitizer. Select an unrepaired source and pass `--expect-defect`
to verify the exact 16-case baseline rejection. The expectation checks the
rejection modes and one-count excess while preserving recursion ownership; it
does not accept arbitrary fixture failures. Compiler/run logs reject above
64 KiB, compiler execution has a 60-second deadline and each fixture has a
five-second deadline. The retained receipt starts with `status=incomplete`.

Passing host fixtures establish the extracted functions' control-flow contract.
Target ARM compilation, generated-code inspection, an attended temporary boot,
and software-event/hotplug lifecycle measurements retain independent gates.
The source defect alone identifies neither an observed phone stall nor a
measured RAM, latency or throughput improvement.
