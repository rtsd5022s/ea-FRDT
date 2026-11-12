# 常见问题（QA）

本文件汇总已核实行为；参数语义以 `PARAMETERS_CN.md`、架构以 `IMPLEMENTATION_CN.md`、实测步数以 `BASELINE_CN.md` 为准，出现细节分歧时以源码与上述文档为准。

## 一、依赖与编译

### Q: Dec128 是否依赖 `__int128`？
**A: 不依赖。** Dec128(BID) 的 BID 路径用自写 `UInt256` schoolbook 大整数乘法，纯 64 位字操作，不用 `__int128`。x87/FP128 的纯软件路径在 64×64 中间积辅助处使用 `__int128`，那是另一路线，不是 Dec128 依赖。

### Q: x87 和 FP128 是否依赖 libquadmath？
**A: 不依赖。** 两者均为纯软件整数仿真，不链接 libquadmath，不使用 `long double` 或 `__float128`。

### Q: MSVC 支持吗？
**A: 不支持。** 项目强制 GNU/Clang 工具链（MSYS2 clang64 / GCC 等）。所有头文件开头都有 `#if defined(_WIN32) && !defined(__MINGW32__) #error ...`，同时排斥 MSVC cl.exe **和** LLVM 官方预编译包默认的 `x86_64-pc-windows-msvc` clang（后者会定义 `_MSC_VER` 但不定义 `__MINGW32__`）。Windows 上必须用 MSYS2 clang64（target `x86_64-w64-mingw32`）。

### Q: 需要第三方库吗？
**A: 不需要。** 零第三方依赖，不引入 Boost/Eigen/libquadmath；实现位于 `include/`、`src/`，十进制 C ABI 静态库在 `libeafrdt/`。

### Q: Windows 静态 exe 需要运行时吗？
**A: `-static` 静态链接后可在 Windows x86_64 直接运行，不附带额外运行时安装；体积随优化级别变化，以实际构建为准。**

### Q: Linux / macOS 能跑吗？
**A: 可从源码构建。** Linux 走 GCC/Clang 与 `pthread`；`physical_cores()` 在 Linux 返回 0（横幅不显示物理核）。发布产物以 Windows 为主，其他平台自行用 CMake 构建。

## 二、格式与步数

### Q: 支持哪些格式？
**A: 共 17 种。默认 `-m all` 链为 13 种：**
FP4 → FP6 → FP8E4M3 → FP8E5M2 → FP16 → BF16 → TF32 → FP32 → Dec32(BID) → FP64 → Dec64(BID) → x87 → FP128。

**默认排除（需 `--only` 显式加入）：** DPD32、DPD64、DPD128、Dec128(BID)。

### Q: 参考步数（1.0 × 1.02 → 溢出）是多少？
**A: 完整参考表见 `BASELINE_CN.md`。** 示例量级：FP32≈4481、Dec32/DPD32≈11279、FP64≈35843、Dec64/DPD64≈44767、x87/FP128 数十万、Dec128/DPD128 七位数。步数主要由位宽与 Emax 决定、对构建较稳定；耗时随机器与优化变化，不是跨构建承诺。

### Q: DPD 和 Dec128 在 `-m all` 默认包含吗？
**A: 默认都不包含。** DPD×3 与 Dec128(BID) 为纯软件慢速路线，默认链排除；用 `--only dpd32,dpd64,dpd128,dec128` 加入，或在单格式 `-m dpd64` 下直接运行（单格式不弹确认门）。

### Q: BID 和 DPD 有什么区别？
**A: BID 把系数当二进制整数，做二进制乘后按 10^n 调整；DPD 把系数当十进制数字串，逐 declet 乘法加进位传播。编码与算术路径不同，均遵循 IEEE 754-2008 的十进制边界（Emax/Emin）。**

## 三、步进模式

### Q: 默认步进是什么？
**A: 乘 `1.02`（`-k 1.02` / `-g x1.02`）。**

### Q: `-k 1` 会怎样？
**A: 1.0×1 仍为 1.0、无进展，退出原因为 `identity->stop`。**

### Q: `-k 0`、负倍率、NaN/Inf 倍率会怎样？
**A: 硬退出码 `2` 并在 stderr 报错；这些不是运行时结果，而是非法输入。**

### Q: factor < 1 会怎样？
**A: 递减行走，从起点向下直到下溢到 `underflow->0`。**

### Q: `-g mululp` / `addulp` / `subulp` 区别？
**A: `mululp` = 乘 `1 + 1 ULP`（相对步长）；`addulp` = 尾数加 1 ULP；`subulp` = 尾数减 1 ULP（递减）。**

## 四、起点

### Q: 默认起点是什么？
**A: 1.0。第一个位置参数是 `delay_ms`（限速），不是起点。**

### Q: 负数起点怎样给？
**A: 用 `-a -1` 之类，从负值向 `-inf` 走；不要把负数当作位置参数。**

### Q: `-a max-finite` / `-a min-normal` / `-s`？
**A: `max-finite` 从最大有限正数起；`min-normal`（别名 `min`）从最小**正规格**正数起；`-s`（`--subnormal`）从最小**非规格**正数起，与 `-a` 互斥。**

### Q: Raw hex 起点怎么给？
**A: `-a 0x3f800000`，按目标格式解释位模式；`0x` 格式非法（长度/字符不符）时硬退出码 `2`。**

## 五、安全与防护

### Q: `-l -1`（无限循环）可以不配对超时吗？
**A: 可启动，但在 `-m all` 会弹一级黄色 `[!]` 确认警告（提示建议配 `-t`），10 秒内按 `y` 继续、否则 `cancelled`；它不是硬拒绝。单格式不弹此门。远程/SSH 运行强烈建议始终配超时。**

### Q: factor 接近 1（很小的相对倍率）会怎样？
**A: 不存在“接近 1 就拒绝”。`-k 1` 是 `identity->stop`；倍率 `< 1e-10` 才在 `-m all` 触发一级警告（可能需要极多步），真正兜底是 `safety->limit`（`2^120` 计数器）。**

### Q: DPD / Dec128 启动前会怎样？
**A: 仅 `-m all`：纳入 DPD×3 或 Dec128 时触发二级红色 `[!!!]` 确认，10 秒内按 `y` 继续、否则 `cancelled`。单格式直接运行、不弹门。**

### Q: 非法输入退出码是多少？
**A: 参数/校验错误退出码 `2`（stderr）；正常结束、`cancelled`、`sigint` 均为 `0`。不存在码 `1` 的约定。**

### Q: 恶意 hex 或 ANSI 转义字符？
**A: 畸形 `0x` 直接拒绝；回显到错误信息的用户输入经 `sanitize()` 过滤 ESC/C0/DEL，防终端注入。**

## 六、并发模式

### Q: `-m all` 做什么？
**A: 每个 worker 绑一个逻辑核、独立跑默认 13 链（或 `--only` 指定的子集），worker 间值拷贝、无共享可变状态、无需加锁。**

### Q: `-j` 怎么写、编号从几开始？
**A: 从 0 开始：`-j 0` 是第一核；范围 `-j 0-3`；列举 `-j 0,2`；可混合 `-j 0,2-4`。解析后丢弃 `v<0 || v>=hw`、排序去重；空（默认）选全部逻辑核；解析为空则退出码 `2`。**

### Q: `-j` 有上限吗？
**A: 有效编号限于 `0..hw-1`；越界编号被丢弃而非单独报错。**

### Q: Worker 有缓存行隔离吗？
**A: 有。`alignas(64)` 让 `WorkerSlot` 独占缓存行，消除伪共享。**

### Q: Ctrl+C 在并发下怎样？
**A: 捕获后置标志、本轮结束后走 `sigint`，双语提示后优雅退出。**

## 七、显示

### Q: 位表示用什么开关？
**A: `--bits hex|bin|off`（`--showbits`），默认 `off`；`bin` 按 sign/exponent/coefficient 分段。不存在 `-x` 开关。**

### Q: `-q` 做什么？
**A: 抑制实时逐步行与结束行，只保留退出时的一行 `总计 <N> 步 · <退出原因> · 墙钟 <t>` 尾行；结果仍写入 `RunResult`。`--json` 独占 stdout，不打印该尾行；`--dump`/`--dump-events` 隐式静默，同样只留尾行。**

### Q: 监视器时间显示规则？
**A: 按已耗时量级逐层渲染（秒 → 分:秒 → 时:分:秒 → 天:时:分:秒…），`\r` 原地刷新。**

## 八、退出原因

退出时可能给出的驱动原因（七枚举）：

- `overflow->inf`：下一步溢出到无穷
- `underflow->0`：下一步下溢到零
- `identity->stop`：下一步等于当前值、无进展
- `safety->limit`：撞到 `2^120` 步数计数上限
- `timeout`：`-t` 超时触发
- `nan->nan`：下一步变 NaN（仅原生 float/double 路径；自写整数格式不产生 NaN）
- `fixed`：仅 opt-in `--steps <n>` 跑满 N 步退出（JSON token `fixed`、终端文案 `fixed-steps`）

外层控制文案（非枚举）：`cancelled`（确认被拒/超时，主要见于 `-m all`）、`sigint`（Ctrl+C）。

## 九、语言

### Q: 默认什么语言？
**A: 默认 `zh`；`--lang en` 切换英文。全部界面文字（含警告、错误、Ctrl+C 提示）走 `tr(zh,en)`。**

## 十、版本与成熟度

### Q: 当前版本号在哪看？
**A: 以仓库 `CHANGELOG.md` 与 git tag 为准；本文不固定版本号。**

### Q: 是稳定版吗？
**A: 处于持续迭代的开发阶段；边界行为以实测与 `BASELINE_CN.md` 参考表为准，跨构建数值可能存在差异。**
