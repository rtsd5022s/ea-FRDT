---
name: Step-count / baseline drift
about: Report a step count that differs from BASELINE, or a perf regression
title: "[BASELINE] "
labels: bug, baseline
---

**Environment**
- OS:
- CPU: (e.g. i7-6700, Ryzen 7 5800X3D)
- Compiler: (clang++ version / g++ version)
- Version: (tag or commit hash)

**Reproducing command**
```
(paste the exact command)
```

**Expected step count**
From `docs/BASELINE_*.md`: (e.g. fp32 overflow → 4481 steps)

**Actual step count**
What you observed, and the exit reason (`overflow->inf` / `underflow->0` / `timeout` / etc).

**If this is a perf regression**
- Baseline wall time: __ s
- Current wall time: __ s
- What changed between the two runs? (commit, compiler flags, machine, thermal state)

**Additional context**
Hex bit pattern of the last step, dump output, or anything else.
