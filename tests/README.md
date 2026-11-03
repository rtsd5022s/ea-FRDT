# tests — 确定性回归（字节序 / 裸流 / `-m all` 几何）

本目录只提交**脚本与断言逻辑**；所有样本 `.bin` 与临时可执行文件生成到 `tests/tmp/`，由根 `.gitignore` 忽略，不入库、不提交。

## 运行前置

- 工具链：MSYS2 **clang64**（GNU/Clang 目标；本项目不支持 MSVC），`clang++` 与 `python` 在 PATH。
- 单条命令一键完成「构建 → 生成样本 → 严格校验」：

```powershell
pwsh tests/run_tests.ps1
# 退出码 0 = 两组校验器 TOTAL_FAILS=0；非 0 = 存在 FAIL
```

脚本内部使用 `C:\msys64\clang64\bin;C:\msys64\usr\bin` 前置 PATH；若你的 MSYS2 路径不同，改脚本顶部对应行即可。

## 文件说明

| 文件 | 作用 | 退出码 |
|---|---|---|
| `run_tests.ps1` | 编排器：5 个 TU 编译 → 生成 36 个样本形态（30 个 LE/BE 对样本 + 6 个裸流/`-m all` 形态）→ 调用两个校验器，汇总成败 | 0 全绿 / 1 失败 |
| `verify_endian.py` | 字节序/宽度几何对拍：LE/BE 互逆、1B 不翻转、模数对齐、无头 magic、首记录位恒等 | 0 无 FAIL / 1 有 FAIL |
| `verify_raw.py` | 裸流行为：`--dump-raw` 与 `--dump-no-header` 字节等价、无 magic、packed 3B 模数、`-m all` 删除空文件、普通 all 留 256B 空壳，并复用 `dump_inspect.py` 校验 v2/all_v2 | 0 无 FAIL / 1 有 FAIL |

> 裸流（raw）无 `EAFRDT2` magic，`dump_inspect.py` 是 v2 专用严格校验器、对裸流判非 v2（退出码 2）；因此 raw 样本只做几何与别名断言，v2 / all_v2 才交给 `dump_inspect.py`。

## 覆盖矩阵（已断言）

| 样本对 | 格式/路径 | 宽度 B | 方向 | 关键断言 |
|---|---|---:|---|---|
| f32_le/be | `drive.h` 原生 `float` | 4 | rev | BE==reverse(LE)、共同前缀零错位、原始位与浮点值双端恒等 |
| f64_le/be | `drive.h` 原生 `double` | 8 | rev | 同上，原始位 `0x3FF051EB851EB852` 双端恒等 |
| tf32_nat_le/be | `engine_sim` 自然对齐 | 4 | rev | 互逆、%4 |
| bf16_nat_le/be | `engine_sim` 自然 | 2 | rev | 互逆、%2 |
| t32pk_le/be | `engine_sim` packed 紧凑 | 3 | rev | 奇数宽度 %3、互逆（中间字节自留） |
| fp8_le/be | `engine_sim` 自然 | 1 | same | 1B 无字节序效应，LE==BE 逐字节相同 |
| x87_le/be | 独立 `dump_cb` | 10 | rev | 互逆、%10 |
| fp128_le/be | 独立 `dump_cb` | 16 | rev | 互逆、%16 |
| dec32_le/be | BID decnum | 4 | rev | 互逆、%4、无 magic |
| dpd32_le/be | DPD 紧凑 | 4 | rev | 共同前缀互逆（条数随性能波动，不断言等长） |
| fp16_le/be | `drive.h` 原生 `_Float16` | 2 | rev | 互逆、%2、无 magic |
| dec64_le/be | BID `engine_decnum` Decimal64 | 8 | rev | 共同前缀互逆、%8、无 magic |
| dec128_le/be | BigDec BID128 `dump_cb` | 16 | rev | 共同前缀互逆、%16、无 magic |
| dpd64_le/be | `engine_dpd` DPD64 | 8 | rev | 共同前缀互逆、%8、无 magic |
| dpd128_le/be | `engine_dpd` DPD128 | 16 | rev | 共同前缀互逆、%16、无 magic |

裸流专项：`v2.bin`（普通头+载荷、inspect 通过）、`raw_le.bin` ≡ `raw_nh.bin`（两别名逐字节相同）、`raw_tf32pack.bin`（%3）、`allraw.bin`（应不存在）、`all_v2.bin`（恰 256B、magic、inspect 通过）。

## 设计口径（为何不断言固定记录条数）

样本由 `-t 1` 限时生成，记录数随主机频率、调度与十进制纯软件算法速度波动（例如 dpd32 LE/BE 条数可不同）。因此校验器**只断言与机器性能无关的不变量**：模数对齐、字节序互逆/相同、别名等价、magic 有无、空壳长度与检查和；不对绝对文件大小/条数写死阈值，避免 flaky。

## 覆盖口径：独立对拍对与模板代表

- 独立 LE/BE 对共 **15 对（30 个文件）**，覆盖全部 ≥2B 记录宽度与三类写盘路径：原生 `drive.h`（f32/f64/fp16）、`engine_sim`（tf32 自然/packed、bf16、fp8）、各独立 `dump_cb`（x87、fp128、`engine_decnum`/BigDec、`engine_dpd`）。
- **1B 无字节序效应族由 `fp8` 的 same 断言代表**：`fp4 / fp6 / fp8_e4m3 / fp8_e5m2` 均走 `engine_sim` 自然 1B，`rec_bytes=1` 时字节交换循环执行 0 次、LE≡BE，与 `fp8_le/be` 同一模板，不再重复造样本；若日后要逐格式留痕，在 `verify_endian.py` 的 `PAIRS` 加一行、`run_tests.ps1` 加对应生成命令即可。
- `dec64/dec128/dpd64/dpd128` 为纯软件十进制，条数受主机速度影响波动（LE/BE 不等长属正常），故只锁共同前缀互逆与 `%宽度==0`，不写死绝对大小。

## 失败排查

- `BUILD FAILED`：先看 `tests/tmp/frdt_test.exe` 构建日志，确认 `clang++` 与头文件路径；0 error 为准，历史已知 warning（未使用形参/预留常量/`#pragma comment`）不阻断。
- `RUN_NONZERO`：单格式运行异常，优先核对模式名与 dump 开关组合。
- `TOTAL_FAILS>0`：输出逐条 `FAIL` 指明文件、宽度与首字节，按互逆/模数/magic/别名四类定位。
