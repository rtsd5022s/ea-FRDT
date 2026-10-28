# 基准测试结果

测试环境：i7-6700 (4C8T), Windows x86_64, clang64 Clang 23, `-Oz -flto=thin -static`。
命令：`ea-FRDT.exe 0 -m <format> -q`（从 1.0 乘 1.02 到溢出）。

## 单线程步数基准（1.0 × 1.02 → overflow）

| 格式 | 步数 | 耗时 |
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

## 说明

- FP4/FP6/FP8 从 1.0 乘 1.02 第一步就 identity(stopped)，因为这些极小格式的 1.0×1.02 舍入后等于 1.0。
- Dec32/DPD32 步数一致（11279），Dec64/DPD64 一致（44767），Dec128/DPD128 一致（714521），证明 BID 和 DPD 编码在相同数学操作下结果一致。
- x87 和 FP128 步数一致（573521），因为两者都是 15 位指数范围。
- Dec128(BID) 最慢（12 秒），纯软件 UInt256 大整数乘法。DPD128 更快（4.9 秒），因为 DPD 用十进制数组逐位运算，比 BID 的大整数乘法快。

## 默认 -m all 范围

默认 `-m all` 包含 13 种格式，排除：
- DPD32/DPD64/DPD128（纯软件十进制，慢 20-30 倍）
- Dec128(BID)（UInt256 大整数乘法，12 秒）

需要用 `--only dpd32,dpd64,dpd128,dec128` 手动添加。

## 命令行参数测试

- `-m fp32`：单格式
- `-m all`：并发全格式（默认排除 DPD/Dec128）
- `--only fp32,f64,dec128`：子集
- `-g mululp` / `-g addulp`：ULP 步进
- `-k 1.01`：自定义倍率
- `-s 0x3f800000`：hex 起点
- `-a max-finite`：最大有限值起点
- `-l 3`：循环 3 次
- `-t 10`：10 秒超时
- `-j 1,3,5`：指定核心
- `-q`：静默模式
- `-lang cn/en`：语言切换

算法本身可能会开小差。
