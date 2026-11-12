# KNOWN_ISSUES — 已知问题、限制与待办清单（防遗忘）

- 用途：集中记录本项目**已出现的缺陷、未入库改动、文档不一致、设计限制、使用风险、已证伪疑点、流程陷阱与待授权决策**，避免反复排查或遗忘。
- 核对基线：本地 `main` = `origin/main` = `8413d2d`（代码级四块翻新 + 五项可用性修复 + DPD 引擎对齐，已 push；CI run `43258487555` 三个 job 全绿；`run_tests.ps1` 75/0、17 格式文本与 51 份 dump 逐字节回归全过）。本条不再写"尚未 push"之类的临时状态。
- 状态图例：`[历史已修]` 已入库、保留作根因档案；`[待决策]` 需用户拍板；`[文档一致性]` 已对齐项 / 待授权项分列；`[设计限制]` 非缺陷、勿误改；`[使用风险]` 运行须知；`[已证伪]` 禁止据此改动；`[流程陷阱]` 方法学/提交纪律；`[覆盖缺口]` 未单独实跑或可选增强。
- 关联文档（本清单只做索引与摘要，不复制全文）：`docs/RISKS_CN.md`、`docs/IMPLEMENTATION_CN.md`、`docs/COMPLEXITY_CN.md`、`docs/TESTING_DISHONESTY_CN.md`、`CHANGELOG.md`、`README.md`。

---

## 1. `[历史已修]` B1–B7 缺陷修复（已入库并 push，保留作根因档案）

基线 HEAD=`2fb2f5a`。本周期为 7 个已跟踪文件（6 个源码 + `tests/run_tests.ps1` 测试设施修复，`git diff --stat` 约 87 insertions / 27 deletions）。质量门：`clang++ -std=c++23 -O2 -Wall -Wextra` 零 warning；`run_tests.ps1` 两处 `TOTAL_FAILS=0`、`ALL TESTS PASSED`、`ec=0`；B1–B7 行为结论见 1.2。设计限制 / 已证伪疑点 / 运营决策不在本节（见第 3、5、6 节，刻意保留、不作为 bug 修改）。

### 1.1 未提交源码文件与主题

| 文件 | 改动主题 | 对应缺陷 |
|---|---|---|
| `include/base_types.h` | 新增 `g_dump_packed_active`（仅实际缩宽才置位）、`g_assume_yes`（`-y/--yes`） | B5、B6 |
| `include/drive.h` | quiet 退出路径不再调用 `slow_update()`，避免覆盖终止值（如 underflow→0） | B7 |
| `include/formats/engine_native.h` | `MinNormal` 改为 IEEE 最小**正规格化**数（f16=2⁻¹⁴；其余 `numeric_limits<T>::min()`），tag=`min-normal` | B2 |
| `include/formats/engine_sim.h` | packed 先算自然字宽 `nat_bytes`，仅 `rec_bytes < nat_bytes` 才置 `g_dump_packed_active` | B5 |
| `src/float_inf_walk.cpp` | 单格式拒绝 `-l/--only/-j`（exit 2）；`g_json_loop` 单格式诚实回写 1；新增 `-y/--yes`；确认取消由 exit 0 改为 exit 1；单格式向 stderr 打印警告；dump 头 `0x0B` 写 `packed_active`；help 文案同步 | B1、B3、B4、B5、B6 |
| `src/format_util.cpp` | eng 格式下 `m` 非有限（f64 denorm 导致 `10^e3` 下溢为 0）时退化 SCI，杜绝 `infe-324` 非法串 | B7 |
| `tests/run_tests.ps1` | Python 探测改为直接扫描 `PATH` 实体 `python.exe/python3.exe/python3`，忽略同名 Alias/Function（`Source` 为空会误报 not found）；测试设施修复，非 B 编号 | 回归入口健壮性 |

### 1.2 B1–B7 修复方向对照（修复前根因位置见 `tests/PARAM_MATRIX_BUG_REPORT.md`，行号已随改动漂移，以机制为准）

| 编号 | 原问题 | 工作树采用方向 | 本轮实测结论 |
|---|---|---|---|
| B1 中 | 单格式 `-l/--loop` 不重复执行，但 JSON `loop` 与 dump 头 `0x54` 照写用户值 | 方向二：单格式遇 `-l` 直接 exit 2；`g_json_loop` 单格式回写 1；拒绝时抑制 JSON shell | 单格式 `-l 2`：ec=2、stdout 0 字节、stderr 中英报错；正常单格式 JSON `params.loop=1` |
| B2 中 | native f16/f32/f64 `-a min-normal` 误赋 `denorm_min`，乘 1.02 恒等提前退出 | 改为最小正规格化数（f16=2⁻¹⁴；其余 `numeric_limits<T>::min()`），tag=`min-normal` | 不再恒等 16 步：fp16 自然 overflow 1076 步 final `65.408000e3`；f32/f64 递增中（timeout 2048 步，f64 final `9.130534e-291`） |
| B3 低-中 | 单格式 `--only`、`-j/--jobs` 静默忽略、无提示 | 单格式检测到即 exit 2 并报中英错误 | `--only f32,f64` 与 `-j 0` 均 ec=2、stdout 0 字节、stderr 报错 |
| B4 低 | 单格式 DPD/dec128 性能警告从不展示 | 单格式向 stderr 打印 level≥1 警告（不做交互确认门） | `dpd128 --json` stdout 369B 为纯 JSON、无 `[!!!]`；211B 警告走 stderr |
| B5 建议 | `--dump-packed` 对整字宽 native 也置头标志，但记录宽不缩减 | 新增 active 位，整字宽格式不置位；头 `0x0B` 写 active 而非请求值 | f32 packed：`0x0B=0`、size=3328；tf32 packed：`0x0B=1`、size=2560；magic 均 `EAFRDT2` |
| B6 低-中 | 非交互（管道/CI）下 critical 警告阻塞 10s，取消时 exit 0、无产物、无 `--yes` | 新增 `-y/--yes`；取消改为 exit 1；警告统一 stderr，保持 stdout 机器可读 | `-y` ec=0、stdout 为可解析纯 JSON（本轮补修：确认警告由 cout 改 cerr，消除 `-m all --json -y` 的 stdout 污染）；取消路径 ec=1 |
| B7 低-中 | f64 denorm/underflow 默认 eng 渲染成 `infe-324`；退出期 `slow_update` 覆盖 `cur_value="0"` | 非有限 `m` 退化 SCI；quiet 退出不再 `slow_update` | f64 `-k 0.5` reason=underflow、`final_value="0"`、steps=1075，无 `infe-324`，终止值未被覆盖 |

### 1.3 未跟踪证据文件

| 路径 | 状态 | 待办 |
|---|---|---|
| `tests/PARAM_MATRIX_BUG_REPORT.md` | 未跟踪（`??`），B1–B7 历史定位证据，含已证伪 P4–P6 原始记录 | 本次**不入库、不删除**，本地留存备查；提交只用精确 `git add --`，不涉及该文件；是否归档改名留待后续单独授权 |

### 1.4 `[待决策]` 历史遗留决策项

| 项 | 现状 | 建议倾向 |
|---|---|---|
| C1 `libeafrdt` 外置 | 主构建/CI 不包含静态库，按 `libeafrdt/README.md` 手动构建 | 维持外置、不改主构建 |
| C3 历史 blob 清理 | 涉及破坏性历史重写 | 不建议执行（收益低、风险高） |
| B1/B2/B6 语义口径 | 已实现并逐项实测通过（见 1.2），不再阻塞提交 | 随本次英文 conventional 分组提交；`push` 与否为独立决策，不混入本次 commit |

### 1.5 提交纪律（适用于上述修复入库）

- 精确 `git add -- <文件>`，禁止 `git add -A` / `git add .`；提交前 `git diff --cached --stat` 核对暂存集；提交后 `git show --stat HEAD`。
- 提交信息用英文 conventional commit；不用 `--amend`、`--force`、`--no-verify`；push 后 `git ls-remote origin refs/heads/main` 直连核对完整 SHA，不信退出码与本地缓存 ref。
- 已按此执行：`b630002` / `171ea81` / `8413d2d` 三笔，`push f3ab53c..8413d2d`，CI 全绿。

### 1.6 代码级翻新与五项可用性修复（`b630002` / `171ea81` / `8413d2d`，已 push）

| 项 | 原状 | 现状 | 防回归点 |
|---|---|---|---|
| 缺位置参数 `delay_ms` | 重打整页帮助、stderr 全空、退出 2 | stderr 一行 `错误：缺少位置参数 delay_ms …`、stdout 空、退出 2 | `run_tests.ps1` 断言 stderr 非空且无帮助页 |
| 未知 `-m` 与 `--json` | 打出半截 JSON 并伪造 `"format":"foo"` | 拒绝路径 `g_json=false` 并删除早开的 `--dump` 空壳 | 契约脚本断言 stdout 0 字节 |
| `-q` 单格式 | 0 行输出 | 一行 `总计 N 步 · reason · 墙钟 t` 尾行 | 尾行只在 `g_single_summary && !g_json` 时打印 |
| 默认单格式刷屏 | 4481 行后无收尾 | 同上尾行收尾 | 三处调用点：正常/静默/超时 |
| 帮助 `-m all` | 写"17 个格式并发""`--only` 默认全部 17" | 13 个并发、`--only` 默认 13 且点名 DPD×3 与 dec128 须显式加入 | 与 `runner.h` 默认链一致 |
| DPD 引擎 | 手写 `run_dpd` 不吃 `--steps`/`--max-seconds`（`--steps 2` 实跑 11,279 步） | 与共享驱动同序检查上限、同样打尾行；自然终止步数不变 | 17 格式 dump 51/51 逐字节回归；DPD 新 dump 为旧全量记录严格前缀 |
| 结构翻新 | `std::function` 类型擦除、六引擎重复起点/落盘代码、`libeafrdt` 自带 `UInt256` 副本 | `Options`+`parse_options`、`drive.h` 模板回调、`engine_emit.h` 共享小件、lib 改为 `#include "../include/uint256.h"` | clang 695,296 B（仍 < 1 MiB 红线）；`-Wall -Wextra` 双零 warning |

---

## 2. `[文档一致性]` 已对齐项与仍需注意的口径（2026-11 全库文档翻新后）

| 位置 | 事项 | 处理状态 |
|---|---|---|
| `CHANGELOG.md` | 断言套件原写 "76 checks"，实测 `CONTRACT_CHECKS=75` | 已改为 75 |
| `CHANGELOG.md:24`（v1.0.6 Fixed 历史行） | "x87 dump 记录宽 10 字节"是 `2fb2f5a` 加宽**前**的事实 | 历史段原样保留，现行口径由 `Unreleased > Changed`（16B = 10 有效 + 6 零填充）承载 |
| `docs/*`（全库 30 篇 + 根 `README.md` / `KNOWN_ISSUES.md`） | 本轮翻新：单格式 `总计` 尾行、拒绝路径不产 JSON、缺 `delay_ms` 报错、帮助 13 口径、DPD 遵守 `--steps`/`--max-seconds`、`cancelled` 退出码 `1`、模块表补 `cui.h` 与 `engine_emit.h`、`drive.h` 模板回调、libeafrdt 共用 `uint256.h` | 已对齐（中英成对） |
| 本文档自身 | 旧版大量引用 `<文件>:<行号>` 形式的定位（如 README 的默认链构建行、`runner.h` 的绑定处），代码一动即漂移 | 本期起只记文件与机制，不再记行号；旧行号引用已就地替换 |
| 线上 Description / Topics | 现状 `Silicon-level devil-grade hardware stress testing` | **未经授权不修改线上元数据**，维持只读 |

---

## 3. `[设计限制]` 平台与能力边界（非缺陷，勿误改）

| 项 | 事实 | 位置/依据 |
|---|---|---|
| 编译器 | 强制 C++23、GCC 13+ 或 Clang 16+；**不支持 MSVC**（使用 GNU/Clang 专有属性与 `__int128` 辅助） | 根 `README.md` 构建/局限小节、`docs/FAQ_CN.md` |
| 已知编译器缺陷 | g++ 16.2.0 在本代码库触发已知 ICE；改用 Clang 16+ 或 g++ 13/14 | 根 `README.md` 局限小节、`GCC_ICE.md` |
| 第三方依赖 | 零依赖：无 libquadmath、无 Boost、无 Eigen；无 `long double` / `__float128` | 根 `README.md`、`docs/IMPLEMENTATION_CN.md` |
| 发布形态 | 预编译二进制仅 Windows x86_64；Linux/macOS 仅源码构建；CI 在 Ubuntu 用 GCC+Clang 跑 smoke，未启用 `-Werror`（warning 不失败） | 根 `README.md`、`docs/IMPLEMENTATION_CN.md` 构建小节 |
| Linux 调度能力 | 仅 `pthread_setaffinity_np` 绑核；**无线程提级、无高分辨率定时器**；`physical_cores()` 返回 0（跳过显示） | `src/platform.cpp` 物理核探测、`docs/IMPLEMENTATION_CN.md` 平台小节 |
| 绑定/提级作用域 | `SetThreadAffinityMask` + `THREAD_PRIORITY_TIME_CRITICAL` 仅作用于 `-m all` worker；单格式直跑不绑核、不提级 | `include/runner.h` worker 绑核处、`src/platform.cpp` 亲和/提级 |
| all 模式 dump | 多 worker 记录宽度交织，不序列化记录；`--dump` 仅写出 flags bit2=empty 的 256B 空头（`payload_bytes=0`）；逐步二进制记录须用**单格式** `--dump` | `CHANGELOG.md` Unreleased、`docs/MODE_ALL_CN.md` dump 小节 |
| 默认格式链 | `-m all` 默认 13/17 格式，主动排除 DPD32/64/128 与 Dec128；慢格式需 `--only` 显式进入 | 根 `README.md`、`include/runner.h` 默认链构建 |
| 慢格式成本 | DPD×3 为纯软件十进制数组运算（约 20–30× 慢，level-2 警告）；Dec128(BID) 为最高成本档，含十进制串 `O(n·m)` schoolbook 与 string/vector 分配 | 根 `README.md`、`docs/COMPLEXITY_CN.md` 慢格式成本 |
| 未提供目标 | 无 GPU offload、无 WebAssembly 目标 | 根 `README.md` 局限小节、`docs/IMPLEMENTATION_CN.md` |
| UInt256 能力 | 无加/减/移位，仅服务乘法与除以 10；`mul128` 为 2×2 字 schoolbook；更宽位宽算法后续需重估 | `docs/IMPLEMENTATION_CN.md` 已知局限、`docs/UINT256_CN.md` |
| `__int128` 使用边界 | `u128` 计数器、x87/fp128 的 64×64 尾数中间积使用编译器 `unsigned __int128`；dec128(BID) 系数路径不用 `__int128`（自研 UInt256） | 根 `README.md` 局限小节、`docs/FAQ_CN.md` |
| 测量口径 | 唯一对标自身基线（FP32=1×、BASELINE 步数、costX 档），不产出跨厂商排名；短样本失真需标注；总步数由数学动态范围决定，不等于单步快慢；`steps/s` 受热降频、调度、频率波动影响 | `COMPLEXITY_CN.md`、`docs/RISKS_CN.md` 测量干扰小节 |
| BID 系数显示 helper 非严格互逆 | `bid_dec_to_bin` 内层用十进制 `/10`、`%10`（逐位），`bid_bin_to_dec` 用二进制 `×2`，二者约定不对称、不保证字符串互逆；`bid_selfcheck()` 系数比较对 `expect` 尾随补零，而 `unpack.coeffDec` 走 `bid_strip0`，口径不同（启动仅打印、返回值不阻断）。两者只服务**显示系数串/诊断打印**，步进算术走独立 UInt 大整数，17 格式确定性步数基线不受影响；**勿为追求字符串互逆改 pack/unpack 位布局**（会破坏 v2 字节几何回归） | `include/formats/engine_bid_codec.h` 显示 helper、`engine_decnum.h`、`dec128.h` |
| W2 `--self-test` 覆盖边界 | 纯逻辑自测只覆盖：CRC-32/IEEE 已知向量、DPD `encode3/decode3` 0..999 往返、BID 编码位宽与 sign/exp 元数据、十六进制解析与 `sanitize`；**不含**上表 BID 显示 helper 互逆，也不跑真实压测/墙钟性能 | `src/float_inf_walk.cpp` `run_self_test()` |

---

## 4. `[使用风险]` 运行须知（详见 `docs/RISKS_CN.md`，此处为防遗忘摘要）

| 层级 | 要点 |
|---|---|
| 调度饥饿 | Windows worker 绑核 + `TIME_CRITICAL`，全核时其他进程（资源管理器、输入法、浏览器、杀毒、同步服务）被挤到时间片夹缝；建议 `-j` 留核给系统 |
| 系统失稳 | 后台任务（Windows Update、Defender、OneDrive、SSH 服务、网络栈）被饿死；小核数/虚拟机双实时 worker 可能让 `dwm.exe`/看门狗超时，触发重启或蓝屏 |
| 热与寿命 | 满载触发热降频，测得是热平衡后速度而非峰值；长期满载加速电迁移；注意电源额定功率留约 30% 余量；笔记本散热与电池、SSD 写入量（TBW）随长时运行受损 |
| 无限循环 | `-l -1` 必须配 `-t`；SSH 断连可能留下孤儿进程持续占 CPU；`nohup`/`systemd-run` 后台化后会长期驻留 |
| 数据与法律 | 压测前保存工作、关闭无关程序；勿用于生产/共享服务器；MIT 无担保，"非典型持续高负载"可能影响厂商保修；跑完确认进程退出、空闲散热 |
| 测量干扰 | 热干扰、后台调度干扰、睿频波动、缓存冷热都会让 `steps/s` 偏离瞬时峰值，数字需结合环境解读 |

---

## 5. `[已证伪]` 历史疑点（结论为非 bug，禁止据此改动）

| 疑点 | 结论 | 依据 |
|---|---|---|
| P4 非静默 `-t` 超时失效 | 证伪：早期脚本管道死锁；程序超时逻辑有效（多个格式 `-t1` 均约 1010ms 退出、Code 0、命中 timeout 文案） | `PARAM_MATRIX_BUG_REPORT.md:82` |
| P5 dec32 `subulp` 恰停 1,000,000 步 | 证伪：自然 underflow（1.0 存为 coeff=1000000、exp=-6，逐位减到 0 恰 1e6）；全仓无 1e6 硬上限（安全上限为 2¹²⁰ 量级） | `PARAM_MATRIX_BUG_REPORT.md:83` |
| P6 `--bits` 对十进制/纯软件不渲染 | 证伪：探测正则过严；各格式位表示实现齐全（hex 无 `0x` 前缀、bin 空格分组属正常格式） | `PARAM_MATRIX_BUG_REPORT.md:84` |

其余已验证为行为正常（非缺陷）：帮助退出 0；未知 flag/模式、缺值、非法 factor/digits/step/bits/起点、特殊起点与数值起点互斥等均 exit 2 且中英 stderr 明确；raw 起点还原、`max-finite` 一步溢出、`-k 1.0` 恒等保护、`--dump` 强制 quiet、坏输出路径 fopen 失败 exit 2 不落文件、all 模式 `--only bogus`/`-j 99` 正确 exit 2；dump 几何与 CRC 自洽。

---

## 6. `[定位与运营既定决策]`（非代码问题，记录以防反复讨论）

- **产品定位（四不像）**：确定性步进遍历器（核心）+ 绑核压测 + 位级多格式参考实现 + 字节级回归/数据产出 + 反基准立场；既非标准 benchmark、烤机工具，也非数学库；唯一可比对象是自身基线。
- **流量策略**：不做社区发布（账号注册与成本原因）；不堆砌大量通用标签与 `benchmark/fast` 类证明型褒义词；刻意走冷门 J/L 型长期曲线，忍受数月黑暗期后由搜索引擎缓慢收录；Traffic 只重点看独立访客（Unique visitors），`views` 多为自身刷新，`cloners` 含 CI/自动化与自身拉取，不等同真人。
- **GitHub 搜索排序口径**：仓库名 > Description > Topics > Stars > 最近活跃度 > README 关键词 > 代码内容 > Watch/Fork > 少依赖 > 明确 License。
- **反基准立场**：行业测试工具受厂商捐赠与榜单导向影响，存在编译器基准特化、窄内核峰值、选择性环境与统计口径等"遮羞布"；本工具不做 benchmark special、不追向量峰值、不进外部排名，以确定性与可复核证据为目标。

---

## 7. `[流程陷阱]` 方法学与提交纪律（防重复踩坑）

1. **慢速 CLI 测试**：慢速/非静默用例统一 `Start-Process` 把 stdout/stderr 重定向到磁盘文件；"先 `WaitForExit` 再同步 `ReadToEnd`"会因子进程卡在 `std::flush` 管道读不到而**伪挂起**，属测试脚本缺陷，不计入程序 bug。
2. **Git 混批工作树**：只精确暂存目标类文件；禁止 `git add -A/.`；提交前后用 `--cached --stat` 与 `show --stat` 核对文件集合与增删行；推送后 `ls-remote` 直连核对完整 SHA。
3. **回归入口**：`tests/run_tests.ps1`（Clang `-std=c++23 -O2 -Wall -Wextra`）；字节序对拍 `tests/verify_endian.py`；裸流对拍 `tests/verify_raw.py`；dump 校验 `dump_inspect.py`（退出码 0 全对 / 1 CRC 或几何不符 / 2 非 v2 / 3 其他）。
4. **产物忽略卫生**：`tests/tmp/**`、`*.bin`、`*.exe`、`gh.exe`、日志文本等已被 `.gitignore` 忽略，不入库。
5. **Python 探测陷阱**：本机 `Get-Command python` 命中同名 **Alias**（`Source` 为空）而非可执行文件，脚本原写法会误报 "python not found" 并在 verifier 前退出；`tests/run_tests.ps1` 已改为直接扫描 `PATH` 中的 `python.exe/python3.exe/python3` 实体（并保留 Application 回退），新增/迁移环境若再失败先查同名别名与 PATH，勿直接归因于脚本逻辑。

---

## 8. `[历史已修]` 已入库缺陷（防回归，摘要自 `CHANGELOG.md`）

- **v1.0.6**：`step_subulp` 在 emin 不再把尾数回绕成全 1；统一 `--dump-endian` 端序逻辑；DPD 标题措辞与超时退出原因；CI `__cpuid` 跨平台化（Linux GCC/Clang 可构建）；无害 warning 消除且不改行为；CI 措辞对齐"无 `-Werror`"；libeafrdt 未用 codec 加 `[[maybe_unused]]`。
- **v1.0.3**：自写格式 min-normal 的 exp 由硬编码 1 改为 `emin(f)`；max-finite 尾数改 `(1<<(M+1))-1`；FP128 max-finite 低位改全 1；非规格化偏置指数在 hex/bin/dump 显示为全 0；native f16/f32 min 的 `denorm_min()` 在本工具链返回 0 的问题被硬编码正确值绕过；DPD min 指数与 underflow 判断修正。
- **v1.0.1**：`bid128_from_double` / `bid128_mul` 正确解析 34 位系数并规整到 10³⁴ 边界（dec128 步数 715048，误差 0.07% 内）。
- **更早**：`-g mululp-down` 已移除；合法步进模式为 `x1.02`（默认）、`mululp`、`addulp`、`subulp`。
- 以上均已入库；回归用例固化在 `tests/`，改动后先跑 `run_tests.ps1` 全绿再提交。

---

## 9. `[覆盖缺口]` 未单独实跑的低价值边界（透明记录）

- 未单独实跑但源码已有保护：`--dump-endian` 非法值（else 分支 exit 2）、显式 `--bits off/none`、`-r` 负值 clamp 到 0、负 delay clamp 到 0。
- B1/B2/B6 等行为本轮已以手动参数实测 + `run_tests.ps1` 全绿覆盖（非法组合退出码、min-normal 正规数起点步数、`-y` 纯 JSON、packed `0x0B` 几何、underflow `final_value="0"`）；把这些断言固化为脚本自动化断言属可选增强（见第 10 节 P2），不阻塞本次提交。

---

## 10. 待办汇总（按优先级；全部需用户授权后执行，不擅自动手）

| 优先级 | 事项 | 状态/前置条件 |
|---|---|---|
| P0 | 工作树 B1–B7 修复零 warning + 全回归全绿 → 按主题分英文 conventional 提交 | **完成**：已 push，CI run `43258487555` 全绿 |
| P1 | `CHANGELOG.md` 补 Unreleased 条目，消解 x87 16B 口径冲突 | **完成**：历史段不改写，现行口径由 Unreleased 承载；本轮又补 5 条（尾行、拒绝路径、DPD 上限、帮助 13 口径） |
| P2 | 可选增强：把 B1–B7 断言固化进脚本（部分已由 `verify_cli_contract.ps1` 覆盖）；高压档交互提示；跨平台温度/节流检测（实现受限）；将 `tests/PARAM_MATRIX_BUG_REPORT.md` 归档改名 | 单独授权、独立提交 |
| P3 | `-m all --only <单个格式> -j 0` 会起与逻辑核数相同的 worker 各跑同一格式（如 `--only dpd32` = 11,279 × worker 数），属"每线程独立跑完整链"的既有设计 | 非缺陷；若要改成"多 worker 分摊同一格式"需重做 worker 与记录归属，**待用户决策** |
| P4 | `libeafrdt/` 是否"全量托管主引擎"（会牵动 `libeafrdt/README.md` 公开步数） | 仅有外置复用 `uint256.h` 的改动；全量托管**未获授权**，不动 |
| P5 | 线上 Description/Topics 是否调整 | 仅在用户明确授权后用 `gh` 只读核对、再受控写入；维持只读 |
| P6 | C1 维持 `libeafrdt` 外置；C3 历史 blob 清理不建议执行 | 维持现状即可，对应任务 #87 |
