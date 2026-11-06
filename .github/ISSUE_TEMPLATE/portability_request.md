---
name: Port to a new platform
about: You got it building on macOS / BSD / ARM / RISC-V and want to upstream it
title: "[PORT] "
labels: portability
---

**Platform**
- OS: (macOS / FreeBSD / NetBSD / OpenBSD / Android / Linux on ARM64 / Linux on RISC-V / other)
- Arch: (x86_64 / arm64 / aarch64 / riscv64 / other)
- Compiler: (clang++ version / g++ version)

**What did you have to change?**
List the files and the diffs:
```
(paste the patch or link to your fork branch)
```

**OS abstraction gaps**
The project uses `platform.cpp` with `#ifdef _WIN32` / `#else`. What POSIX calls did you add?
- Thread affinity:
- High-res timer:
- Core count:
- SIGINT handling:

**Did it actually work?**
- [ ] `cmake -B build && cmake --build build` passes
- [ ] Smoke test: `./float_inf_walk 0 -m fp32 -t 5 -q`
- [ ] `pwsh tests/run_tests.ps1` (or equivalent shell) green
- [ ] 17-format step counts match `docs/BASELINE_*.md`

**CI note**
This project only tests Linux (GCC/Clang) and Windows (MSYS2 clang64) in CI. We will not add your platform to CI until at least two contributors report it working. But we will merge the portability patches if they are behind `#ifdef` and do not break existing jobs.
