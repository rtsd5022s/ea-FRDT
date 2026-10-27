# 并发模式（-m all 多线程）

## 什么是并发模式

并发模式就是多线程同时跑，每个线程独立跑完整的 17 格式链。线程之间不共享状态，每个线程有自己的当前值、步数、结果列表。

命令行加 `-m all` 就进入并发模式：

```
float_inf_walk 0 -m all
float_inf_walk 0 -m all -j 1-4
float_inf_walk 0 -m all -t 10 -q
```

并发模式和普通模式最大的区别：不是一次跑一个格式，而是每个线程都跑全套 17 种格式。线程数由 `-j` 控制，每个线程绑到指定核心上。

## 启动流程

`-m all` 走进 `run_all` 函数：

1. 解析核心列表（`-j` 参数），确定起几个线程、绑哪些核心
2. 打印横幅：CPU 型号、逻辑核心数、物理核心数、倍率、限速、线程数、格式数
3. 为每个 worker 创建 WorkerSlot 结构，`alignas(64)` 对齐缓存行
4. 起 worker 线程池，每个线程值拷贝参数，绑核，跑格式链
5. 起监视器线程，定期刷新屏幕显示每个 worker 状态
6. 等所有 worker 跑完
7. 打印汇总表

## 核心编号解析

`-j` 参数支持四种写法：

- **单值**：`-j 4` 只用第 4 核
- **逗号分隔**：`-j 1,3` 用第 1 和第 3 核
- **范围**：`-j 1-4` 用第 1 到第 4 核
- **混合**：`-j 1,3-5` 第 1、3、4、5 核

编号从 1 开始，不是从 0。`-j 1` 是第一个逻辑核心。

解析逻辑在 `parse_core_list`：先按逗号分割，每段如果有 `-` 就按范围展开成列表。结果是一个核心编号数组。

硬上限是 `std::thread::hardware_concurrency()`，超过会被截断。8 核 CPU 上 `-j 1-16` 会被截断成 1-8。

## WorkerSlot 结构

每个 worker 一个 WorkerSlot：

```cpp
struct alignas(64) WorkerSlot {
    RunResult cur;
    std::vector<RunResult> done;
    int loop_done = 0;
    u128 total_steps = 0;
    std::vector<u128> sum_steps;
};
```

`alignas(64)` 是关键：把整个结构对齐到 64 字节（CPU 缓存行大小）。这样每个 worker 的数据独占一个缓存行，多核同时读写时不会互相踩到对方的缓存行，避免伪共享（false sharing）。

各字段：
- `cur`：当前正在跑的格式的结果
- `done`：这一轮所有格式的结果列表
- `loop_done`：当前循环次数
- `total_steps`：累计总步数（u128 防溢出）
- `sum_steps`：每个格式累计了多少步

## Worker 线程

每个 worker 线程做什么：

```
绑核到指定核心
重置循环计数
while (没跑完) {
    循环计数++
    if (超时) break
    清空 done 列表
    for (每个格式 in 格式链) {
        cur = 新建空结果
        跑这个格式
        把 cur 加入 done
        total_steps += cur.steps
        sum_steps[格式] += cur.steps
    }
}
```

参数是值拷贝的，每个 worker 有自己的一份，不共享。格式链也是值拷贝的 vector。

## 绑核

每个 worker 线程绑到指定核心：

- **Windows**：`SetThreadAffinityMask(GetCurrentThread(), 1 << (core_id - 1))`，同时设 `THREAD_PRIORITY_TIME_CRITICAL` 提高优先级
- **Linux**：`pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset)`

跨平台逻辑在 `platform.h` 里隔离，worker 代码只调 `fw::plat::bind_thread_to_core(core_id)`。

绑核的好处：线程不会在核心间漂移，每次都在同一个核心上跑，缓存命中率高。不绑核的话 OS 调度器可能把线程挪来挪去，缓存全凉。

## 监视器线程

监视器线程是单独的一个线程，负责刷新屏幕：

```
while (worker 没跑完) {
    sleep(refresh_ms)
    清屏
    for (每个 worker) {
        打印 [wN] 格式名 当前值 速度 耗时 loop
    }
}
```

刷新间隔 `-r` 控制：
- `-m all` 默认 1000ms（1 秒）
- 单线程模式默认 0（每步刷新）

监视器每行格式：

```
[w1] FP32: 1.234567e+10   4500000 steps/s   1.234s   loop=1/3
```

时间显示是逐层的：不到 1 分钟只显示秒，超过 1 分钟显示分:秒，超过 1 小时显示时:分:秒，超过 1 天显示天:时:分:秒。

## 超时

超时是全局的，所有 worker 共享 `g_timeout_sec` 和 `t0`。每个 worker 在每轮循环开始前检查：

```
elapsed = now - t0
if (elapsed >= g_timeout_sec) break
```

超时后 worker 退出，等其他 worker 也退出后打汇总表。

## 循环

`-l 3` 跑 3 遍格式链。每遍跑完（所有格式都溢出/下溢）后，清空 done 列表，重置 cur，从头开始下一遍。

`-l -1` 无限循环，配合 `-t` 使用。

循环模式下监视器显示 `loop=1/3`、`loop=2/3`、`loop=3/3`。无限循环显示 `loop=1/-1`。

## 汇总表

所有 worker 跑完后，每个 worker 打一节：

```
--- worker 1 ---
格式        单次步数    累计步数    退出           耗时ms   总耗时
FP32        4481        13443       overflow->inf  0.5      1.5
FP64        35843       107529      overflow->inf  4.2      5.7
...
总计: 120972 steps, 5.7s = 21223 steps/s
```

列：
- 格式名
- 单次步数：这一轮这个格式走了多少步
- 累计步数：所有循环加起来
- 退出原因：overflow->inf / underflow->0 / timeout / cancelled
- 单次耗时：这一轮这个格式跑了多久
- 总耗时：从开始到现在的墙钟时间

最后一行总计：总步数、总墙钟时间、平均速度。

## DPD 警告

如果格式链里包含 DPD32/DPD64/DPD128，启动前弹黄色警告：

```
[!] DPD algorithm is computationally heavy, performance may drop 20-30x,
higher precision even slower. Press y to continue, auto-cancel in 10s...
```

按 y 继续，10 秒内没按自动取消。Windows 下用 `_kbhit`/`_getch` 非阻塞检测键盘，Linux 下用 `std::cin.read` 阻塞读。

`-m all` 默认不包含 DPD 格式，需要手动 `--only dpd32,dpd64` 之类指定。

## 总速度汇总

汇总表最后一行打总速度：所有 worker 的步数加起来，除以总墙钟时间。

```
总计: 120972 steps, 5.7s = 21223 steps/s
```

这是所有核一起跑的总吞吐。单核跑 FP32 大约 4-5M steps/s，8 核一起跑理论上 32-40M steps/s。但格式链里有慢格式（x87/FP128/DPD），平均下来会低一些。

## 格式链顺序

17 格式跑的顺序：

```
FP4 → FP6 → FP8E4M3 → FP8E5M2 → FP16 → BF16 → TF32 → FP32
→ Dec32 → DPD32 → FP64 → Dec64 → DPD64
→ x87 → FP128 → Dec128 → DPD128
```

从小到大，先小格式后大格式。每个 worker 都跑这个完整链。

`--only` 参数可以指定格式子集：`--only fp32,fp64` 只跑 FP32 和 FP64。

## 性能考虑

并发模式的性能瓶颈：

- **绑核**：消除核心迁移，缓存命中率高
- **alignas(64)**：消除伪共享，多核不互相踩缓存行
- **值拷贝**：worker 之间不共享数据，不需要锁
- **监视器独立**：监视器不挡 worker，只定期读 WorkerSlot

x87 和 FP128 是纯软件整数运算，比原生格式慢。DPD 有查表开销。这些是计算量决定的，不是并发模型的问题。

## 监视器刷新

监视器默认 1 秒刷新一次。这个间隔是 `-r` 参数控制的。`-r 500` 就是 500ms 刷新一次。

刷新太频繁会拖慢 worker，因为监视器要清屏、重画、和 worker 抢 CPU。默认 1 秒是平衡值。

单线程模式下 `-r` 默认 0，每步都刷新，因为没有性能压力。

## 格式子集

`--only` 参数指定格式子集，逗号分隔。比如 `--only fp32,fp64` 只跑 FP32 和 FP64。

这在快速测试时有用：不想跑完整 17 格式链，只测几个格式。

## 性能扩展

并发模式的性能优化点：

- **绑核**：消除核心迁移，线程一直在同一个核心上跑，缓存热
- **alignas(64)**：WorkerSlot 独占缓存行，多核不互相踩
- **值拷贝**：worker 之间不共享，无锁无竞争
- **监视器独立**：监视器不挡 worker，只读 WorkerSlot

如果要进一步压榨性能，可以调大 worker 数（硬上限是逻辑核心数），或者把刷新间隔拉长。

## 横幅信息

启动时打印横幅：

```
=== ALL modes: each worker runs the full format chain ===
CPU: Intel(R) Core(TM) i7-6700 CPU @ 3.40GHz
CPU cores(logical)=8 (physical=4)
step=multiplied, x1.02, delay=0ms, workers=4, chain=17
```

内容包括：CPU 型号、逻辑核心数、物理核心数、倍率、限速、worker 数、格式数。

## 命令行示例

```
# 全部格式并发
float_inf_walk 0 -m all

# 绑 1-4 核
float_inf_walk 0 -m all -j 1-4

# 绑第 1 和第 3 核
float_inf_walk 0 -m all -j 1,3

# 限时 10 秒
float_inf_walk 0 -m all -t 10

# 静默模式
float_inf_walk 0 -m all -q

# 只跑 FP32 和 FP64
float_inf_walk 0 -m all --only fp32,fp64
```

## 输出重定向

并发模式输出到 stdout。静默模式 `-q` 只打汇总表，可以重定向到文件：

```
float_inf_walk 0 -m all -q > summary.txt
```

监视器模式下输出是实时刷新的，不适合重定向。

## 退出码

- 0：正常退出
- 1：参数错误或运行时错误

## 颜色输出

并发模式用 ANSI 颜色代码：青色横幅、灰色信息、绿色正常、黄色警告。监视器实时刷新时用颜色区分 worker。

不支持 ANSI 的终端会显示乱码。静默模式 `-q` 减少颜色输出。

## 语言

`--lang zh` 中文界面，`--lang en` 英文界面。

## 注意事项

1. `-m all` 每个 worker 都跑全套 17 格式，不是分配不同格式给不同 worker。
2. `-j` 编号从 1 开始，硬上限是逻辑核心数。
3. DPD 格式不在 `-m all` 默认链里，要手动加。
4. `alignas(64)` 是为了消除伪共享，去掉会导致多核缓存乒乓变慢。
5. `-r` 刷新间隔默认 1 秒，太频繁会拖慢 worker。
6. 监视器时间显示是逐层的，不会一直显示秒。
7. 汇总表的"单次步数"是最后一遍的步数，不是平均。
8. 无限循环 `-l -1` 必须配合 `-t`，否则永远跑下去。
9. 绑核在容器/虚拟机里可能失败，权限不够时 worker 不绑核照跑。
10. 静默模式 `-q` 不打监视器，跑完直接出汇总表。
11. 总速度是所有 worker 步数除以总墙钟时间，是多核总吞吐。
12. 格式链里小格式先跑先结束，大格式后跑后结束。worker 不会因为小格式结束就提前退出，会等全部跑完。
13. CPU 型号在横幅里打印，物理核心数和逻辑核心数分开显示。
14. worker 之间不共享任何数据，不需要互斥锁，不会有锁竞争。
15. 监视器读 WorkerSlot 是无锁的，因为 worker 写的是自己的内存，监视器只读。

## 使用风险

并发模式把所有核心跑满，长时间运行有几个实际风险，用之前心里有数。

**系统卡顿和响应变慢**。每个 worker 绑核后优先级提到 TIME_CRITICAL，系统其他进程会被挤到剩下的核心上。8 核 CPU 跑 8 个 worker 时，系统基本没法做别的事，鼠标拖动都可能掉帧。这不是 bug，是绑核+高优先级的预期行为。要保留系统响应，就少绑几个核，比如 4 核 CPU 只跑 `-j 1-2`，留两核给系统。

**散热和降频**。长时间满载，CPU 温度上去后会自动降频。笔记本尤其明显，散热不够的机器跑几分钟就开始 throttle，速度会掉。台式机风道好的问题小一些。这个程序是压测工具，设计上就是把 CPU 推到极限，别指望它温和运行。

**电费**。满载 CPU 功耗比空闲高很多，台式机 100W+，笔记本也会飙到 65W+。长时间跑对电费有感知。服务器上跑无所谓，笔记本电池模式下跑会掉电很快。

**DPD 格式的额外风险**。DPD 编解码是纯查表+逐 declet 处理，CPU 占用比 BID 高 20-30 倍。如果在并发模式里加了 DPD 格式，那几个 worker 会把核心吃得更死，其他 worker 可能因为调度延迟变慢。DPD 警告确认不是吓唬人，是真的会让系统更卡。

**无限循环的风险**。`-l -1` 无限循环如果忘了加 `-t`，程序会永远跑下去。虽然 Ctrl+C 能停，但如果是 SSH 远程跑的，断开连接后进程可能变成孤儿进程继续吃 CPU。务必配合超时使用。

**看门狗和 OOM**。有些系统有 watchdog，CPU 100% 跑太久可能触发看门狗重启。WorkerSlot 每个 worker 占几 KB 内存，不会 OOM，但监视器和线程栈会占一点。内存不是瓶颈，CPU 才是。

**测量偏差**。绑核+高优先级是为了精确测量，但如果系统有其他后台进程（杀毒、Windows Update、同步盘），它们会在后台抢 CPU，测出来的速度会波动。要干净的测量环境，最好关掉后台服务，或者在容器里跑。

**不要在生产环境跑**。这个工具是压测/测试工具，不是生产工具。在生产服务器上跑会拖慢其他业务。测试环境里玩就行。

**数据准确性**。并发模式每个 worker 独立跑格式链，结果和单线程模式应该一致。但如果发现步数对不上，先检查是不是绑核失败导致调度漂移，或者系统干扰。纯软件格式（x87/FP128）在多核并发下应该和单核结果一致，因为是纯整数运算没有共享状态。

