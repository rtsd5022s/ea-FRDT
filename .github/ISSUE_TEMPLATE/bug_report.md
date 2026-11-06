---
name: Bug report
about: Report a crash, wrong output, or step-count drift
title: "[BUG] "
labels: bug
---

**Environment**
- OS: (Windows / Linux / other)
- CPU: (e.g. Intel i7-6700, AMD Ryzen 7 5800X)
- Compiler: (clang++ version / g++ version; MSVC is not supported and will be closed)
- Version: (tag or commit hash)

**Command line**
```
(paste the exact command you ran)
```

**Expected behavior**
What you expected to happen. If you expected a specific step count, cross-check `docs/BASELINE_*.md` first.

**Actual behavior**
What actually happened. Include:
- Last few lines of output
- The step number it died / drifted at
- The hex bit pattern of the last value (if `--bits hex`)

**Additional context**
Screenshots, `--dump-events` excerpt, or anything else.
