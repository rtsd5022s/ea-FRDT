# ea-FRDT

17 种浮点格式的溢出行走器，C++23 写的，能跑多线程压测。

## 编译

GCC 13+ 或 Clang 16+，需要 C++23。

```bash
g++ -std=c++23 -O2 -march=native \
    -Iinclude src/float_inf_walk.cpp src/lang.cpp src/format_util.cpp src/drive.cpp \
    -o float_inf_walk
```

Windows 下加 `-lwinmm`，静态链接加 `-static -static-libgcc -static-libstdc++`。

## 跑起来

```bash
# 默认 FP32 从 1.0 每次乘 1.02 到溢出
./float_inf_walk 0

# FP16 从最大值每次乘 0.5 下溢到 0
./float_inf_walk 0 -m fp16 -a max-finite -k 0.5

# 全部格式并发跑，绑 1~4 核
./float_inf_walk 0 -m all -j 1-4

# 限时 10 秒
./float_inf_walk 0 -m all -t 10
```

## 支持的格式

原生：f16 f32 f64

自写位级：fp4 fp6 fp8-e4m3 fp8-e5m2 bf16 tf32

纯软件：x87(80位) fp128(112位)

十进制：dec32/64/128(BID) dpd32/64/128(DPD)

## 常用参数

| 参数 | 说明 |
|---|---|
| `-m` | 格式，见上 |
| `-a` | 起点，支持负数、0x raw、min-normal、max-finite |
| `-k` | 倍率，默认 1.02，>1 溢出 <1 下溢 |
| `-g` | mululp / addulp 逐 ULP 走 |
| `-j` | 核心编号，单值/逗号/范围混合 |
| `-t` | 超时秒数，0=跑到底 |
| `-l` | 循环次数，-1 无限 |
| `-q` | 静默，直接出汇总表 |

## 目录

```
include/   头文件
src/       源文件
docs/      英文版 README
```

## License

MIT
