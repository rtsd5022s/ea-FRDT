# Baseline Test Results

Test environment: i7-6700 (4C8T), Windows x86_64, clang64 Clang 23, `-Oz -flto=thin -static`.
Command: `ea-FRDT.exe 0 -m <format> -q` (from 1.0 times 1.02 to overflow).

## Single-thread step baseline (1.0 × 1.02 → overflow)

| Format | Steps | Time |
|---|---|---|
| FP4 (E2M1) | 1 (identity) | 0 ms |
| FP6 (E3M2) | 1 (identity) | 0 ms |
| FP8 (E4M3) | 1 (identity) | 0 ms |
| FP8 (E5M2) | 1 (identity) | 0 ms |
| FP16 | 574 | 9 ms |
| BF16 | 3913 | 16 ms |
| TF32 | 4623 | 21 ms |
| FP32 | 4481 | 30 ms |
| Dec32 (BID) | 11279 | 45 ms |
| DPD32 | 11279 | 57 ms |
| FP64 | 35843 | 209 ms |
| Dec64 (BID) | 44767 | 229 ms |
| DPD64 | 44767 | 230 ms |
| x87 (80-bit) | 573521 | 3900 ms |
| FP128 | 573521 | 3911 ms |
| Dec128 (BID) | 714521 | 12244 ms |
| DPD128 | 714521 | 4868 ms |

## Notes

- FP4/FP6/FP8 hit identity(stopped) on the first step from 1.0 because 1.0×1.02 rounds back to 1.0 in these tiny formats.
- Dec32/DPD32 match (11279), Dec64/DPD64 match (44767), Dec128/DPD128 match (714521), confirming BID and DPD produce identical results under the same arithmetic.
- x87 and FP128 match (573521) because both have 15-bit exponent range.
- Dec128 (BID) is slowest (12 seconds) due to pure-software UInt256 big-integer multiply. DPD128 is faster (4.9 seconds) because DPD uses decimal digit-by-digit arithmetic, faster than BID's big-integer multiply.

## Default -m all scope

Default `-m all` includes 13 formats, excluding:
- DPD32/DPD64/DPD128 (pure software decimal, 20-30x slower)
- Dec128 (BID) (UInt256 big-integer multiply, 12 seconds)

Use `--only dpd32,dpd64,dpd128,dec128` to add them manually.

## Command-line argument tests

- `-m fp32`: single format
- `-m all`: concurrent all formats (default excludes DPD/Dec128)
- `--only fp32,f64,dec128`: subset
- `-g mululp` / `-g addulp`: ULP stepping
- `-k 1.01`: custom factor
- `-s 0x3f800000`: hex start
- `-a max-finite`: max finite start
- `-l 3`: loop 3 times
- `-t 10`: 10 second timeout
- `-j 1,3,5`: pin to specific cores
- `-q`: quiet mode
- `-lang cn/en`: language switch

The algorithm may misbehave occasionally.
