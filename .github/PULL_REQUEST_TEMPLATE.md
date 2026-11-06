<!--
Thanks for contributing to ea-FRDT.
Fill this out; incomplete PRs may be asked for more info.
-->

## What does this PR do?

<!-- One paragraph. Link issues: Closes #123 -->

## Type of change

- [ ] Bug fix (non-breaking fix)
- [ ] New feature (non-breaking addition)
- [ ] Breaking change (changes existing step counts, CLI flags, or dump format)
- [ ] Documentation update
- [ ] Build / CI / tooling

## Which formats are affected?

<!-- Tick all that change. If ticking any non-native format, run_tests.ps1 must be green. -->

- [ ] None (docs/CI only)
- [ ] FP4 / FP6 / FP8-E4M3 / FP8-E5M2
- [ ] FP16 / BF16 / TF32 / FP32 / FP64
- [ ] X87 / FP128
- [ ] Dec32 / Dec64 / Dec128 (BID)
- [ ] DPD32 / DPD64 / DPD128

## Test plan

- [ ] Built with `clang++ -std=c++23 -O2 -Wall -Wextra -I include ...` — zero warnings.
- [ ] Ran `pwsh tests/run_tests.ps1` — all contract tests pass.
- [ ] Ran `pwsh tests/run_format_matrix.ps1` — 17/17 step-count baseline unchanged.
- [ ] If touching `include/uint256.h` / `uint128.h`: self-test passes.
- [ ] If touching dump format: `python dump_inspect.py <file>` validates.

## Checklist

- [ ] I did not add third-party dependencies (no Boost/Eigen/libquadmath).
- [ ] Binary size did not exceed the 1 MiB CI red line.
- [ ] No MSVC- or windows-msvc-clang-only code; the `_WIN32 && !__MINGW32__` guard still fires.
- [ ] Bilingual strings go through `lang.h` (no hardcoded English/Chinese in `src/`).
- [ ] I updated `docs/` if CLI flags, formats, or behavior changed (CN and EN together).
- [ ] Comments are in English.

## Notes for reviewers

Anything else reviewers should know: tradeoffs, known follow-ups, why this approach over alternatives.
