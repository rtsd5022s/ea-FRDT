# 参数组合矩阵本地测试 · 缺陷清单（只定位，未改源码）

- 构建：`tests/tmp/param_probe.exe`，`-std=c++23 -O2 -Wall -Wextra`，Clang 23.1.3（MSYS2 clang64），非静态，非发布产物，已被 `.gitignore` 忽略。
- 范围：穷举 CLI 参数组合（格式链、起点、倍率、步进模式、显示/位数、bits、loop、jobs/only、timeout、dump 字节流、`-m all` 并发、单格式引擎对照），实跑取证。
- 边界：本轮**仅找 bug，不修改任何源码**；取证后 `git status --porcelain` 为空，工作树干净。
- 测试方法学备注：慢速/非静默用例统一 `Start-Process` 重定向 stdout/stderr 到磁盘文件；早期“先 `WaitForExit` 再同步 `ReadToEnd`”会因父进程不读管道、子进程卡在 `std::flush` 而伪挂起，属脚本缺陷，不计入程序 bug。
- 结论摘要：**无崩溃、无程序自身挂起、无段错误、无记录字节损坏**；命中 7 项缺陷/观察，其中 2 项中等、3 项低到中、2 项低/建议；另有 3 项历史疑点已证伪（见末节，勿改）。

## 一、成立缺陷总表

| 编号 | 严重度 | 类别 | 一句话结论 | 关键复现命令 | 根因位置 |
|---|---|---|---|---|---|
| B1 | 中 | 静默失效 + 元数据谎报 | 单格式 `-l/--loop` 不重复执行（仍单遍），但 JSON `loop` 与 dump 头照写用户值 | `0 -m f32 -l 2 --json`；`0 -m f32 -l2 --dump f1b.bin` | `float_inf_walk.cpp:526,606-622`；`runner.h:262`（仅 all 真循环） |
| B2 | 中 | 名实不符（错误起点语义） | native f16/f32/f64 的 `-a min-normal` 赋成**最小非规格化数 denorm_min**，非最小正规格化数，恒 identity16 | `0 -m f64 -a min-normal --json` | `engine_native.h:37-42`；对照正确：`engine_decnum.h:138` |
| B3 | 低-中 | 静默忽略选项 | 单格式下 `--only`、`-j/--jobs` 不报错也不生效，仅 `-m all` 消费 | `0 -m f32 --only f32,f64 --json`；`0 -m f32 -j 0 --json` | `float_inf_walk.cpp:562-581` vs `605-622` |
| B7 | 低-中 | 输出不自洽（非法文本） | f64 denorm/下溢在默认 eng 格式渲染成非法串 `infe-324`；underflow 的 `cur_value="0"` 被退出后渲染覆盖 | `0 -m f64 -a min-normal --json`；`0 -m f64 -k 0.5 --json` | `format_util.cpp:51-55`；`drive.h:60,136-137` |
| B4 | 低 | 警告收集后丢弃 | 单格式收集的 DPD/dec128 critical 性能警告从不展示，仅 all 模式确认 | `0 -m dpd128 -t2 --json`（stderr 为空） | `float_inf_walk.cpp:534-561,562-563` |
| B6 | 低-中 | 可用性 / 退出码语义 | `-m all` 非交互（管道/CI）下 critical 警告阻塞 10s 后以退出码 0 取消、不产 JSON，且无 `--yes` 旁路 | `0 -m all --only dpd128 --json` | `float_inf_walk.cpp:20-50,563` |
| B5 | 建议 | 标志语义澄清（无数据错误） | `--dump-packed` 对整字宽 native（如 f32）仅置头标志、记录宽仍 4 字节，不压缩 | `0 -m f32 --dump-packed --dump f4.bin` | `float_inf_walk.cpp:594`；`drive.h:81-95` |

严重度口径：中＝影响结果正确性或直接违背用户预期；低-中＝可用性/一致性缺口或易被自动化误判；低＝提示缺失；建议＝语义需澄清、输出本身无误。

## 二、逐条证据与修复方向（待授权，本轮不改）

### B1 单格式 `-l/--loop` 静默失效，JSON/dump 头谎报 【中】
- 实测：`0 -m f32 -l 2 --json` → `steps=4481`（单遍，未翻倍），`params.loop=2`；`-l -1 -t1` 仍单遍自然 overflow、`loop=-1`；`-l 0` 单遍、`loop=1`。`-l2 --dump f1b.bin`：`record_count=4480`、头偏移 0x54 `Loop=2`，记录数仍是单遍值。
- 对照（证明 loop 机制本身存在且正确）：`-m all --only f32 -l 2 --json` 每个 worker `steps=4481`、`sum_steps=8962`、`total_steps=71696=8×8962`；`-l1` 时 `sum_steps=4481`、`total=35848=8×4481`。
- 根因：循环只存在于 `runner.h` all worker（`while(infinite||loops<loop_count)`）；单格式分发 `run_fp16/fp32/...` 签名不含 `loop_count`，`float_inf_walk.cpp:606-622` 直接返回；但 `g_json_loop=loop_count`（:526）与 dump 头 0x54 仍写用户原值。
- 修复方向（二选一，需拍板）：
  1. 让单格式也按 loop_count 重复驱动，并让 JSON `steps`/`sum_steps` 反映轮数；
  2. 单格式下对 `-l` 报错或明确告警“loop 仅 `-m all` 有效”，且 loop 无效时 JSON/dump 头回写 1 而非用户原值。

### B2 native `min-normal` 赋成 denorm_min，名实不符 【中】
- 实测（同一命令族，原始 JSON）：

| 格式 | reason | steps | final_value | 评价 |
|---|---|---|---|---|
| f16 | identity | 16 | 5.9604645e-8 (2⁻²⁴) | denorm_min，错误起点 |
| f32 | identity | 16 | 1.401298e-45 (2⁻¹⁴⁹) | denorm_min，错误起点 |
| f64 | identity | 16 | infe-324（见 B7，量级 2⁻¹⁰⁷⁴） | denorm_min，错误起点 |
| dec32 | overflow | 22326 | 9.973295e+96 | 语义正确（最小正规格化后递增溢出） |

- IEEE 参考：最小**正规格化**数应为 f16≈2⁻¹⁴(6.1035e-5)、f32≈2⁻¹²⁶(1.1755e-38)、f64≈2⁻¹⁰²²(2.2251e-308)；当前赋值是最小**非规格化**数，乘 1.02 舍入不变，命中 IDENT_LIMIT=16 提前退出，无法施压。
- 根因：`engine_native.h:37-42` 三分支分别写死 `2⁻²⁴ / 2⁻¹⁴⁹ / denorm_min()`，`start_tag="min"`；且 :36 另有独立 `StartKind::Subnormal`（tag=`min-subnormal`，同样赋 `denorm_min`），语义重叠。十进制 `engine_decnum.h:138` 为 coeff=1、exp=-(emax−1)、tag=`min-normalized`，实现正确，可作为统一口径参照。
- 修复方向（需拍板）：
  1. native MinNormal 改为 IEEE 最小正规格化数；
  2. 若产品语义确实想要 denorm，则把 CLI/文档/枚举改名为 subnormal/denorm，并消除与 `Subnormal` 的重复。

### B3 单格式 `--only` / `-j` 被静默忽略 【低-中】
- 实测：`-m f32 --only f32,f64` 仍只输出 f32；`-m f32 -j 0` 无任何行为变化、无提示。
- 根因：`jobs_list=parse_jobs(...)` 与 `only_csv` 仅在 `if(mode=="all")`（:562-581）使用；单格式分支不读取，解析阶段也不校验“当前模式不支持该选项”。
- 修复方向：单格式检测到 `--only`/`-j` 时给 stderr 提示或返回 2；或在 help/文档标注“仅 all 有效”。

### B7 f64 极小值默认 eng 渲染为非法串 `infe-324` 【低-中】
- 实测：`f64 -a min-normal --json` 与 `f64 -k 0.5 --json`（reason=underflow）的 `final_value` 均为 `"infe-324"`，不是合法浮点文本；underflow 语义上应为 0 或明确下限值。
- 根因：
  - `format_util.cpp:51-55`：eng 格式 `e=floor(log10|v|)`、`e3=floor(e/3)*3`、`m=v/pow(10,e3)`；当 `e3≤-308`（f64 denorm，如 e3=-324）时 `10^e3` 在 double 下溢为 0，`m=v/0=+inf`，`%.6fe%d` 拼出 `inf` + `e-324` = `infe-324`。
  - `drive.h:60` underflow 先写 `cur_value="0"`，但退出前 `drive.h:136-137` 无条件 `slow_update()` 再 `render(s)` 覆盖该值。
- 影响范围：仅 double 通道且量级 <1e-308；f32（最小约 1e-45，e3=-45）不触发；纯软件十进制/DPD/x87/fp128 自带渲染不经此 double eng 路径。
- 修复方向：`e3` 过小时退化为 SCI 或直接输出最小可表示量；underflow 命中后不再被退出期 `slow_update` 覆盖（区分“终止值”与“周期刷新值”）。

### B4 单格式 critical 性能警告不展示 【低】
- 实测：单格式 `dpd128 / dec128 / dpd32` 的 stderr 为空；`-m all --only dpd128` 才会先打印 `[!!!]` 性能警告。
- 根因：警告向量 `warns` 在 `:534-561` 收集（含单格式 dpd/dec128 的 level2、以及 `-l -1` 无超时的 level1），唯一消费点 `confirm_warnings(warns)` 在 `:562-563` 的 `if(mode=="all")` 内。
- 修复方向：单格式分支在驱动前打印 level≥1 警告到 stderr（不强制交互确认），保持与 all 的信息一致性。

### B6 非交互缺确认开关，取消时退出码 0 且无产物 【低-中】
- 实测：`0 -m all --only dpd128 --json` 打印 critical 警告后约 10s 输出“已取消/Cancelled”，无 all JSON、退出码 0。
- 根因：`confirm_warnings`（:20-50）Windows 用 `_kbhit` 轮询 10s；非交互/重定向 stdin 无按键即 `confirmed=false → return false → main return 0`；代码无 `--yes/-y/--assume-yes`，也未探测 stdin 是否 tty。
- 风险：CI/管道自动跑会被阻塞约 10s，且退出码 0 易被误判为成功，却拿不到机器可读结果。
- 修复方向：新增非交互确认开关；或 stdin 非 tty 时按配置直接继续、或以非 0 退出并明确提示需要确认。

### B5 `--dump-packed` 对整字宽 native 不缩减（建议项） 【建议】
- 实测：f32 `--dump-packed` 头 `packed=1`，但 `record_size=4`、`payload_bytes=17920=4480×4`，与未置 packed 相同；几何自洽、无损坏。对照 tf32 packed：`record_size=3`、`payload_bytes=13866=4622×3`，压缩生效。
- 根因：native 无 `dump_cb`，走 `drive.h:81-95` 的 `sizeof(S)` 默认分支；`float_inf_walk.cpp:594` f32 `record_size=4`。packed 仅对非整字宽的 sim 格式（tf32 等）有实际缩减。
- 修复方向（建议）：对 `record_size` 已等于自然字宽者不置 packed 位，或文档注明“packed 仅作用于非整字宽格式”。

## 三、已证伪/排除（非 bug，禁止据此改动）

| 历史疑点 | 结论 | 依据 |
|---|---|---|
| P4 非静默 `-t` 超时失效 | 证伪＝早期脚本管道死锁；程序超时有效 | `f32/f16 addulp -t1` 文件通道均约 1010ms、Code0、命中 timeout 文本后正常退出；quiet/JSON 路径同样有效（dpd32 addulp -t1=1746512 步 timeout；fp128 mululp -t1=3214592 步 timeout） |
| P5 dec32 `subulp` 恰停 1,000,000 步 | 证伪＝自然 underflow，非硬截断 | 1.0 存为 coeff=1000000、exp=-6；`dec_subulp` 每步 coeff−=1，到 0 恰 1e6；全仓无 1e6 步硬上限（SAFETY=2¹²⁰） |
| P6 `--bits` 对十进制/纯软件不渲染 | 证伪＝探测正则过严；实现齐全 | native/sim/decnum/dec128/dpd/x87/fp128 均输出位表示；fp8-e4m3 为 `(38)`、f32 bin 为空格分组位串，hex 无 `0x` 前缀，旧 `HasBits` 正则漏判 |

其余已验证为**行为正常**（非缺陷）：`-h`=0；未知 flag/模式、缺值、非法 factor/digits/step/bits/起点、特殊起点与数值起点互斥、`min-normal` 与 `-s` 互斥，均 exit2 且中英 stderr 明确；`0x3F800000` raw 起点正确还原 f32=1.0；`max-finite`×1.02 一步溢出；`-k 1.0` identity 保护、`-k 0.5` 递减 underflow；`mululp/addulp/subulp` 均合法；`--dump` 强制 quiet；raw/no-header 同义；坏输出路径 fopen 失败 exit2 且不落文件；all 模式 `--only bogus`、`-j 99` 正确 exit2；默认 all 链 13 格式、8 worker、`total_steps=10020688`，各格式 reason/步数与历史基线逐项吻合；dump 几何（Size=256+payload、Rcount=payload/rsize、tf32 packed 字节数、raw 无头 17920B）与 CRC 自洽。

## 四、覆盖与未跑边缘（透明缺口）

- 已覆盖：17 格式 ×1.02 基线复核、三步进模式、三类数值起点/raw/max-finite/min-normal、factor>1/=1/<1、显示 fmt 与 digits、bits hex/bin/off、loop、jobs/only、timeout、dump 全套（header/raw/packed/endian）、all 并发、单格式慢引擎（dpd128/dec128/dpd32）。
- 未单独实跑、但源码已明确的低价值边界（不影响主结论）：`--dump-endian` 非法值（`float_inf_walk.cpp:486` else exit2）、显式 `--bits off/none`、`-r -1` clamp 到 0（:440）、负 delay clamp（:513）。
- 待用户拍板的语义决策：B1 修复方向、B2 语义口径（改实现 vs 改命名）、B6 是否需要 `--yes`；确认授权后方可进入修复阶段，且修复提交须为英文 conventional commit、`git status --porcelain` 核对仅预期文件变更、非强推。
