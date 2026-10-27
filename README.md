# ea-FRDT — Floating Point Range Deep Tester

C++23 浮点溢出行走器，支持 17 种浮点格式，多线程并发压测。

## 功能

- **17 种浮点格式**：FP4 / FP6 / FP8(E4M3/E5M2) / FP16 / BF16 / TF32 / FP32 / Dec32[BID] / DPD32 / FP64 / Dec64[BID] / DPD64 / x87(80位) / FP128(112位) / Dec128[BID] / DPD128
- **纯软件实现**：x87 / FP128 / DPD 全部自写，无 libquadmath 依赖
- **IEEE 754-2008 十进制浮点**：BID + DPD 双路线
- **多线程 ALL 模式**：每线程独立状态机，绑定 CPU 核心
- **安全防御**：输入校验、超时兜底、DoS 防护、DPD 性能警告确认

## 编译

需要 GCC 13+ 或 Clang 16+，C++23 支持。

```bash
g++ -std=c++23 -O2 -march=native -static -static-libgcc -static-libstdc++ \
    -Iinclude src/float_inf_walk.cpp src/lang.cpp src/format_util.cpp src/drive.cpp \
    -lwinmm -o float_inf_walk.exe
```

Windows 下需 `-lwinmm`，Linux 下去掉。

## 使用

```bash
# 默认：FP32 从1.0 ×1.02 到 inf
float_inf_walk.exe 0

# 指定格式和倍率
float_inf_walk.exe 0 -m fp16 -k 1.5

# 从最大值递减到0
float_inf_walk.exe 0 -a max-finite -k 0.5

# ALL 并发模式（全部格式，多线程）
float_inf_walk.exe 0 -m all -j 1-4 -q

# 超时控制
float_inf_walk.exe 0 -m all -t 10

# 循环
float_inf_walk.exe 0 -m all -l 3 -t 10
```

## 命令行参数

| 参数 | 说明 |
|---|---|
| `-m <mode>` | 浮点格式（f16/f32/f64/fp4/fp6/fp8/bf16/tf32/x87/fp128/dec32/dec64/dec128/dpd32/dpd64/dpd128/all） |
| `-a <x>` | 起点（数字/科学/工程/0x Raw/min-normal/max-finite，支持负数） |
| `-k <r>` | 每步倍率（默认1.02，支持科学计数法） |
| `-g <s>` | 步进模式（x1.02默认/mululp/addulp） |
| `-f <f>` | 显示格式（eng默认/sci/full） |
| `-d <n>` | sci/eng 小数位（默认6，-1=完整，0~100） |
| `--bits <m>` | 括号内位表示（hex/bin/off默认） |
| `-j <spec>` | 核心编号（单值4/逗号1,3/范围1-3/混合） |
| `--only <list>` | 逗号分隔格式子集 |
| `-r <ms>` | 刷新间隔（0=每步，-m all默认1000） |
| `-t <s>` | 超时停止秒数（0=直到inf/下溢默认） |
| `-l <n>` | 循环次数（-1=无限，1=关闭默认，>1=N次） |
| `-s` | 从最小非规格化数开始 |
| `-q` | 静默模式（直接出汇总表） |
| `--lang <zh\|en>` | 界面语言 |
| `delay_ms` | 每步限速毫秒（0=不限速） |

## 目录结构

```
include/          头文件
  base_types.h    枚举/u128/RunResult
  lang.h          双语接口
  color.h         ANSI 颜色
  format_util.h   格式化工具
  drive.h         驱动器模板
  fp_formats.h    自写位级格式 + x87/FP128
  decimal.h       BID 十进制浮点
  dpd.h           DPD 十进制浮点
  runner.h        多线程 ALL 模式
  uint256.h       自研大整数
src/              实现文件
  float_inf_walk.cpp  main + 参数解析
  lang.cpp        双语实现
  format_util.cpp 格式化实现
  drive.cpp       驱动器实现
```

## 许可证

MIT
