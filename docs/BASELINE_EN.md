# Baseline Test Results

Test environment: i7-6700 (4C8T), Windows x86_64, clang64 Clang 23, `-Oz -flto=thin -static`.
Command: `ea-FRDT.exe 0 -m <format> -q` (from 1.0 times 1.02 to overflow, no timeout).

## Step baseline (1.0 × 1.02 → overflow)

| Format | Steps | Time |
|---|---|---|
| FP4 (E2M1) | 1 (identity) | 0 ms |
| FP6 (E3M2) | 1 (identity) | 0 ms |
| FP8 (E4M3) | 1 (identity) | 0 ms |
| FP8 (E5M2) | 1 (identity) | 0 ms |
| FP16 | 574 | 18 ms |
| BF16 | 3913 | 16 ms |
| TF32 | 4623 | 25 ms |
| FP32 | 4481 | 19 ms |
| Dec32 (BID) | 11279 | 53 ms |
| DPD32 | 11279 | 59 ms |
| FP64 | 35843 | 174 ms |
| Dec64 (BID) | 44767 | 244 ms |
| DPD64 | 44767 | 269 ms |
| x87 (80-bit) | 573521 | 4526 ms |
| FP128 | 573521 | 4398 ms |
| Dec128 (BID) | 714521 | 13119 ms |

## Notes

- FP4/FP6/FP8 hit identity(stopped) on the first step from 1.0 because 1.0×1.02 rounds back to 1.0 in these tiny formats.
- Dec32/DPD32 step counts match (11279), Dec64/DPD64 match (44767), confirming BID and DPD produce identical results under the same arithmetic.
- x87 and FP128 step counts match (573521) because both have 15-bit exponent range.
- Dec128 is slowest (13 seconds) due to pure-software UInt256 big-integer multiply.

## Command-line argument tests

- `-m fp32`: single format
- `-m all`: concurrent all formats
- `-g mululp` / `-g addulp`: ULP stepping
- `-k 1.01`: custom factor
- `-s 0x3f800000`: hex start
- `-a max-finite`: max finite start
- `-l 3`: loop 3 times
- `-t 10`: 10 second timeout
- `-j 1,3,5`: pin to specific cores
- `-q`: quiet mode
- `--only fp32,f64`: subset
- `-lang cn/en`: language switch

The algorithm may misbehave occasionally.
