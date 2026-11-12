# 并发模式（`-m all` 多线程）

## 什么是并发模式

`-m all` 每个被选中的逻辑核起一个 worker；每个 worker 独立跑默认格式链，worker 之间不共享可变状态，各自持有当前值、步数与结果列表。

默认链共 **13 种格式**：默认排除 DPD32/DPD64/DPD128 与 Dec128(BID)，需 `--only` 显式加入。

```
float_inf_walk 0 -m all
float_inf_walk 0 -m all -j 0-3
float_inf_walk 0 -m all -t 10 -q
```

与单格式模式的区别：不是一次跑一个格式，而是每个 worker 反复遍历整条链；worker 数与核心集合由 `-j` 控制，每个 worker 绑到指定核心。

## 启动流程

`-m all` 进入 `run_all`：

1. 解析 `-j` 核心规格，确定 worker 数与亲和性。
2. 打印横幅：CPU 型号、逻辑核、物理核（Linux 下返回 0、隐藏该字段）、倍率、限速、worker 数、链长。
3. 为每个 worker 建 `alignas(64)` `WorkerSlot`。
4. 起 worker 线程：值拷贝参数、绑核、跑链。
5. 起监视器，按 `-r` 刷新。
6. join 全部 worker。
7. 打汇总表；`-q` 抑制实时监视器、保留汇总表。

## 核心编号解析（`-j` / `--jobs`）

**从 0 开始**：编号直接索引逻辑处理器，第一核是 `0`。空规格（默认）选中全部逻辑核 `0..hw-1`。

- 单值：`-j 3` = 核 `3`
- 逗号：`-j 0,2` = 核 `0、2`
- 范围：`-j 0-3` = 核 `0,1,2,3`
- 混合：`-j 0,2-4` = `0,2,3,4`

`parse_jobs` 先按逗号分割、展开 `-` 范围，再丢弃 `v<0 || v>=hw`，排序去重；解析为空则退出码 `2`，报错提示合法范围 `0~(hw-1)`。

## WorkerSlot

```cpp
struct alignas(64) WorkerSlot {
    RunResult cur;
    std::vector<RunResult> done;
    int loop_done = 0;
    u128 total_steps = 0;
    std::vector<u128> sum_steps;
};
```

`alignas(64)` 把结构对齐到 CPU 缓存行，避免多核伪共享。字段：当前格式结果、本轮结果列表、循环计数、`u128` 累计总步数、每格式步数累加器。

## Worker 线程

```
绑核
循环计数 = 0
while (!done) {
    循环计数++
    if (超时) break
    清空 done
    for (格式 : 链) {
        cur = 新建 RunResult
        跑该格式
        done.push_back(cur)
        total_steps += cur.steps
        sum_steps[格式] += cur.steps
    }
}
```

参数与链 vector 均为值拷贝，worker 互不读对方栈。

## 绑核

- **Windows**：`SetThreadAffinityMask(GetCurrentThread(), 1ULL << core_id)`（无偏移），并 `SetThreadPriority(..., THREAD_PRIORITY_TIME_CRITICAL)`。
- **Linux**：`pthread_setaffinity_np` + `CPU_SET(core_id, ...)`；提权走 best-effort：先试 `SCHED_BATCH`（不需 root），失败退 `nice(-5)`；容器/VM 无 CAP_SYS_NICE 时静默不提。

跨平台调用经 `platform.cpp` 的 `fw::plat::bind_thread_to_core`。绑核减少迁移、保缓存热度；容器/VM 无权限时绑定失败，worker 降级为不绑核照跑。

## 确认警告（此门仅存在于 `-m all`）

`confirm_warnings` 在 worker 启动前汇总非致命警告；10 秒内按 `y` 继续，否则本次 `cancelled`、退出码 `1`（`-y/--yes` 可跳过确认，供 CI/管道使用）：

- 一级黄色 `[!]`：`-l -1` 配 `-t 0`；倍率 `< 1e-10`。
- 二级红色 `[!!!]`：链含 DPD32/DPD64/DPD128（慢速软件 declet）；链含 Dec128（慢速 UInt256 算术）。均为警告而非拒绝。

单格式调用不经过该确认门。

## 监视器

独立线程睡 `-r`（`-m all` 默认 `1000` ms；单格式按步刷新），每 worker 一行：

```
[w1] FP32: 1.234567e+10   4.50e6 steps/s   1.234s   loop=1/3
```

耗时逐层渲染（秒 → 分:秒 → 时:分:秒 → 天:时:分:秒），`\r` 原地刷新。

## 超时与循环

全局 `g_timeout_sec` 与共享起始时间约束所有 worker，到期原因为 `timeout`。`-l 3` 跑三遍链；监视器显示 `loop=1/3`…；`-l -1` 无限重复。`-l -1` 配 `-t 0` 仅触发上述一级警告；仍强烈建议配超时，尤其 SSH 远程。

## 汇总表

join 后每 worker 一节，列出每格式单遍步数、累计步数、退出原因、单遍与累计耗时；末行为总步数、总墙钟时间与总吞吐。退出原因取自七枚举（`overflow->inf`、`underflow->0`、`identity->stop`、`safety->limit`、`timeout`、`nan->nan`、`fixed`（仅单格式 opt-in `--steps <n>` 触发）），外加外层 `cancelled` / `sigint`。

## 默认链顺序（13）

```
FP4 → FP6 → FP8E4M3 → FP8E5M2 → FP16 → BF16 → TF32 → FP32
→ Dec32 → FP64 → Dec64 → X87 → FP128
```

`--only` 指定子集或追加慢格式，如 `--only dpd32,dpd64,dpd128,dec128`；`--only fp32,fp64` 只跑这两个。链按小到大排列，小格式先结束，但 worker 会等完整条链。

## 性能要点

绑核消除迁移；`alignas(64)` 消除伪共享；值拷贝消除加锁；监视器只读 Slot。x87/FP128 为纯软件、DPD 为逐 declet 算术，链吞吐主要由这些慢格式决定。实时优先级仅 Windows 生效；吞吐受后台负载与热降频影响而波动。

## 使用风险（压测预期内行为）

- **系统响应**：Windows 下 worker 绑核且高优先级，留给其他进程的核心更少；4 核机建议 `-j 0-1` 留核给系统。
- **散热与功耗**：长时间满载升温并可能降频，笔记本/弱散热机器数分钟内降频；功耗远高于空闲。
- **慢格式长载**：DPD×3 与 Dec128 让 worker 满载更久，故默认链排除。
- **无限循环**：`-l -1` 无超时只能靠 Ctrl+C；SSH 断开可能留孤儿进程，务必配 `-t`。
- **资源占用**：Slot 仅 KB 级，内存非瓶颈、CPU 才是；部分平台看门狗对长时间满载敏感。
- **勿用于生产**：这是压测工具，不是生产负载。
- **结果准确性**：纯软件整数格式步数与单核一致；出现分歧先查绑核失败或外部干扰。
- **`--dump` 在 all 模式的行为**：并发 worker 的记录宽度不一且互相交织，故 all 模式不序列化任何记录；即便传了 `--dump`，也只写出 flags bit2=empty 的 256 字节空头部（`payload_bytes=0`、`record_count=0`、`record_size=0`、`reason=0xFF`）。需要逐步二进制记录请改用单格式 `-m <fmt> --dump`。
