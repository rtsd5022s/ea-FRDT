---
name: Feature request
about: Suggest a new CLI flag, format, or output mode
title: "[FEAT] "
labels: enhancement
---

**What problem does this solve?**
One paragraph. Why does the current CLI not already cover it?

**Proposed interface**
What flag / format / output would you like? Example:
```
./float_inf_walk 0 -m fp32 --my-new-flag <value>
```

**Which existing flags does it interact with?**
- Does it conflict with `-a` / `-s` / `-g`?
- Does it apply in single format only, or also `-m all`?
- Does it change step counts (must not), or only display/output?

**Why this belongs in ea-FRDT**
We are deliberately narrow: deterministic float walkers, bit-level formats, no GPU/GUI/telemetry. Make the case.

**Alternatives considered**
What else did you think of, and why is this better?
