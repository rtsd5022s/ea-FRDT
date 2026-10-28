# ea-FRDT 1.0-deta1

ea-FRDT（Extreme Architecture - Floating-point Rigorous Devil Test）首个公开测试版。这是一个 C++23 命令行浮点压测工具，从一个浮点值出发，按固定步长反复步进（乘系数、乘相对 ULP、加绝对 ULP），直到格式溢出到无穷、下溢到零、撞到安全上限或超时。

## 这个版本包含什么

17 种浮点格式全覆盖：FP4、FP6、FP8 E4M3、FP8 E5M2、FP16、BF16、TF32、FP32、Dec32[BID]、DPD32、FP64、Dec64[BID]、DPD64、x87（80 位纯软件）、FP128（112 位纯软件）、Dec128[BID]（自写 UInt256 大整数乘法）、DPD128。

五种步进模式：默认乘 1.02、乘 1+1ULP、加 1ULP、从 max-finite 递减、从 min-normal 递增。

并发模式：`-m all` 每个 worker 绑核独立跑完整套格式链，`alignas(64)` 缓存行隔离消除伪共享，Windows 用 `SetThreadAffinityMask` 绑核，监视器每秒刷新，汇总表按 worker/格式列出步数和退出原因。

安全防护：无限循环必须配超时、factor 太接近 1 拒绝、非法输入硬退出、DPD/dec128 启动前黄色警告按 y 确认、Ctrl+C 双语提示。

双语界面：`--lang zh` / `--lang en`，所有文本（含警告、错误、Ctrl+C 提示）都分中英文。

无第三方依赖：不需要 libquadmath、Boost、Eigen。x87 和 FP128 是纯软件整数仿真，不依赖 `long double` 或 `__float128`。Dec128 用自写 UInt256 schoolbook 大整数乘法，不依赖 `__int128`。

## 平台

仅 Windows x86_64（UCRT64 静态链接，无需安装运行时 DLL）。Linux/macOS 可从源码编译（CI 在 Ubuntu 上验证 GCC 和 Clang）。

## 快速开始

```
ea-FRDT.exe 0                          # FP32 从 1.0 乘 1.02 走到 inf
ea-FRDT.exe 0 -m fp16 -a max-finite -k 0.5   # FP16 最大值递减到 0
ea-FRDT.exe 0 -m all -j 1-4            # 4 个 worker 并发跑全部格式
ea-FRDT.exe 0 -m dec128 -t 10 -q        # Dec128 10 秒超时静默模式
```

完整参数见 `README.md`。

## 局限性

- 仅在 Windows x86_64 上测试，Linux/macOS 未在目标硬件验证（CI 只做编译验证）。
- g++ 16.2.0 本地有内部编译器错误（ICE），请用 Clang 18+ 编译。
- DPD 路线的系数 declet 串联子单元是技术债，能跑但不够紧凑。
- x87/FP128 纯软件实现仍用 `__int128` 做 128 位辅助，未完全替换为 UInt256。
- OSS-Fuzz 未接入，边界值覆盖靠人工测试矩阵。
- 物理核心数检测在 Linux 上未实现。
- 这个版本是 deta（测试）版，可能存在未发现的边界 bug。算法本身可能会开小差。
