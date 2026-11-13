# Why ea-FRDT is a "neither-fish-fowl" project

## 1. What "neither-fish-fowl" means

Tools that touch "floating-point + hardware + performance" split into five categories by user intent: stress/burn-in benchmarks, floating-point benchmarks, educational visualizers, compiler/numeric-library tests, and decimal-float codec libraries. ea-FRDT brushes up against all five but belongs to none.

| Category | Typical tools | What they do | What they don't |
|---|---|---|---|
| Stress / burn-in | Prime95, OCCT, AIDA64 Stress, Cinebench | Heat up CPU/GPU, validate stability | Bit-exactness, per-step values, float-format reference |
| Float benchmark | STREAM, LINPACK, CoreMark, Dhrystone | Measure GFLOPS / bandwidth / MIPS for leaderboards | Long walks, boundary values, non-optimizable outputs |
| Education / visualization | phet float playground, float.stefan.com.br | Type a number in a browser, see its bits | Hardware stress, dec floats, long walks |
| Compiler / numeric tests | boost.math, libFuzzer, glibc math tests, llvm test-suite | Test math functions, find compiler bugs | Hardware pressure, boundary walks, bit streams, throughput |
| Codec libraries | libdecnumber, mpdecimal, Intel RDFP | Decimal float conversion and arithmetic | Stress, walks, priority, dump streams |

ea-FRDT has a piece of each:
- **Hardware stress** like burn-in tools (core pinning, real-time priority, 0ms delay, ALU saturation)
- **Reproducible step counts** like benchmarks (fixed 17-format overflow baselines)
- **Bit-level reference** like educational tools (`--bits bin/hex` per-step output)
- **Determinism** like compiler tests (same start, same flags, same step sequence forever)
- **Format coverage** like codec libraries (17 formats from fp4 to fp128, both BID and DPD decimal paths)

But it is "not good enough" for any single category:
- Burn-in users want a GUI, temperature curves, vendor certification — none here
- Benchmark users want GFLOPS leaderboards, cross-vendor comparisons — none here; this is an anti-benchmark
- Education users want a browser, interactivity — none here; pure CLI
- Compiler tests want a unit-test framework, coverage, CI integration — none here; only a `--self-test` subcommand
- Codec users want a linkable `.a` / `.so` / C ABI — only a light `c_api.h`, not the main goal

That is "neither-fish-fowl": every kind of user finds it "sort of familiar but not quite", and no single category owns us.

## 2. Why we are not a burn-in tool

Burn-in tools' business model forces them to **keep vendors happy**:
- Prime95's Small FFT deliberately does not press the boundary, because pushing too hard makes users hit WHEA errors and demand refunds.
- OCCT ships a "custom error tolerance", because the true hardware boundary blue-screens machines.
- AIDA64 takes hardware-vendor sponsorship, so it cannot push CPUs into unstable territory.
- Cinebench is a marketing product; pretty scores matter more than bit-exactness.

Their "fig leaves":
- SIMD peak packed as score, but ALU utilization is incomplete.
- Hyper-threading inflates the number because pressure lands on shared execution units, not on a pinned core.
- Thermal throttling is hidden; reports show peak, not average.
- Error detection is sampled, not per-step.
- Deliberate margin so an hour-long run never triggers protection.

ea-FRDT does none of that:
- Core-pinned (`--pin`), no hyper-threading free lunch — HT gives only 1.1–1.3× per-core on this workload because we burn scalar ALU, not SIMD pipes.
- 0ms delay, no idle breathing.
- Every step is an exact float multiply the compiler cannot optimize away (results are printed, dumped, written).
- No temperature, no frequency, no GFLOPS — only step counts.
- No sponsorship; behavior does not change for anyone.
- Real-time priority plus best-effort `SCHED_BATCH` on Linux to crush scheduler jitter.

Burn-in users will call us "unprofessional" — no curves, no error detection, no UI, no preset. But we were never trying to serve them. They ask "is my CPU stable"; we ask "how many steps from 1.0 to infinity, and what is the hex at each". Orthogonal questions.

## 3. Why we are not a benchmark

Benchmarks exist to **rank**. Ranking requires: standardized workload, cross-platform comparability, optimizability, an objective "good/bad" score.

ea-FRDT is the opposite:
- The workload is "walk from 1.0 to infinity". The step count depends only on the IEEE 754 format, not on CPU performance. fp32 overflows at 4481 steps on every CPU forever.
- Cross-platform step counts must be identical (this is the feature) — therefore no optimization.
- No GFLOPS, only "how many seconds to finish fp32's 4481 steps" — and that second count depends on thermals, scheduler, DVFS, foreground/background, so it is not rankable.
- No score, only step sequences and exit reasons.
- Deliberately no SIMD, because SIMD makes numbers non-comparable across architectures.

Benchmark users find us "no numbers to compare". We do not compare CPUs; we verify that the same float sequence is bit-exact across hardware. Change the compiler, change libc, change the kernel scheduler, run ea-FRDT, if step counts change you broke something. That is the anti-benchmark: benchmarks measure speed, we measure correctness.

## 4. Why we are not educational software

Educational tools want visualization, interactivity, browser execution, zero install. We are a roughly 680 KB CLI that requires MSYS2 on Windows, no GUI, no web page.

An education user wants "type 0.1+0.2 and see why it is not 0.3". We want "starting from 0.1, multiply by 1.02 each step, count overflow steps, dump every hex, and compare BID vs DPD bit divergence at that value". Different needs.

Our docs have bit tables, BID combo5 derivation, UInt256 schoolbook analysis — far denser than any online tutorial, but they are reference material for people who already know IEEE 754, not an intro course. Beginners will not read them; we do not aim to attract beginners.

## 5. Why we are not a compiler test

Compiler tests (boost.math, libFuzzer, glibc math testsuite, llvm test-suite) need: unit-test framework, coverage, CI integration, fixed input corpora, shipped with the compiler.

We are a standalone CLI binary, no unit-test framework (only a `--self-test` for pure logic), no coverage, not wired into any third-party CI, not shipped with any compiler.

Our side-effect is compiler testing: change glibc's decimal128 soft-fp, run ea-FRDT, if dec128 steps shift from 714521 you broke something. Change clang's x87 codegen, if x87 steps shift, the codegen regressed. Passive value, not active positioning. We will not be imported into the llvm test-suite or wired into CTest, because that would trap us in the "compiler test" category and lose the freedom of being neither-fish-fowl.

## 6. Why we are not a codec library

libdecnumber / mpdecimal / Intel RDFP are linkable libraries: `#include <decfloat.h>`, call `decimal32_add()`, get a result. They:
- Do no stress, no pinning, no real-time priority.
- Do no walks, no "1.0 to infinity".
- Do no bit streams, no dump.
- Do not unify 17 formats.
- Do not run a worker pool.

We ship a `libeafrdt/` with a `c_api.h`, but it is a side product, not the goal. The main goal is the CLI walker. People may link the library, but we make no ABI-compatibility promise, no semver, no stable API. That keeps us distinct from a real decimal-float library.

## 7. Concrete advantages of the neither-fish-fowl position

### 7.1 No competitors in the search gap

Google "prime95 decimal128" — no result.
"fp128 overflow step count" — no result.
"BID DPD schoolbook 256-bit multiplier" — us.
"x87 80-bit vs fp128 step comparison" — us.
"bit-exact float walker stress CPU" — us.
"dpd128 pack unpack self check" — no second open-source implementation.

Each phrase alone is obscure, but together they occupy a **completely empty niche**. To build this product, someone must simultaneously:
- Genuinely know all of IEEE 754 (BID/DPD decimals, x87, fp128, fp8)
- Be willing to write 17 software-emulated format engines
- Be willing to reject MSVC
- Be willing to ship a roughly 680 KB CLI instead of a GUI
- Be willing to skip leaderboards and marketing
- Be willing to write 4000–8000 word technical docs instead of a one-page README

These are AND conditions, not OR. Drop one and the niche stays empty.

### 7.2 Users arrive with their own problem

Ordinary tools attract users by marketing — people did not know they needed it until they saw an ad. Those users open issues like "can you add a GUI", "can you support MSVC", "can you sync to the cloud".

Neither-fish-fowl users arrive **already holding a problem**:
- They are writing a decimal128 compiler runtime and need a reference.
- They are testing a new CPU's ALU and need a deterministic scalar-pipe load.
- They are writing an IEEE 754 paper and need a bit-exact step baseline.
- They are debugging an x87 emulator and need the true overflow point.
- They are auditing a finance pipeline's decimal rounding and need a reference step sequence.

They do not need to be sold. They open GitHub, search a very specific question, find us, clone, run, and either never file an issue (they just use it) or file a high-quality one ("your `bit_length()` on ARM64 mishandles `__builtin_clzll"`, "DPD128's pack misses a declet on subnormal boundaries").

### 7.3 No benchmark, no endless comparisons

L-type repos live with a stream of "how does this compare to Rust's X", "vs Python's decimal", "vs Intel RDFP". Those questions are unanswerable because we are none of those things.

The neither-fish-fowl position makes those questions disappear automatically: no benchmark. We are not a library, not education, not burn-in, not a benchmark. To compare us you first have to decide on what dimension. That "no benchmark" state is protective — it keeps the project from being kidnapped by external expectations.

### 7.4 Long-term value does not expire with tech stacks

Prime95 expires because AVX-512 changes Small FFT. Cinebench expires because new ISAs need new scores. STREAM expires because memory-bandwidth standards move. SPEC CPU expires because vendors tune for it.

IEEE 754 does not expire. fp32 overflows after 4481 steps from 1.0 — that number was 4481 in 1985 and will still be 4481 in 2085. BID dec128's 110-bit coefficient is a fixed standard field. x87's 80-bit layout is 1980s frozen. DPD's 3-decimal-digits-into-10-bits declet is an IEEE 754-2008 annex that will not change.

Our baseline table is a **mathematical fact**, not an engineering optimization. Mathematical facts do not expire. In five years someone building new float hardware still needs this step table. In ten years someone studying IEEE 754 decimal encoding still needs this BID/DPD comparison. That is the physical basis of the J-curve long tail: our asset is not code, it is the 17 fixed step counts and their bit patterns.

## 8. Costs of the position

### 8.1 Why there is no channel push: cost and accounts

The direct reason we do not push through channels is not "nobody would accept the content" but **operating cost and account overhead**: every channel needs registration, real-name verification, account warm-up, continuous posting, comment replies, and a restart after throttling or a ban. That is time and money the project does not spend. The category mismatch is only the second layer — even matching content would still have no budget or account behind it.

The following is only an observation that precision would be poor even with that investment; it is not an absolute "cannot spread" verdict:

V2EX has a "share creations" node, but what category are we? C++? Hardware? Open source? None fit cleanly. Posting there invites "what is this? burn-in? education?" and the explanation cost is huge.

Reddit's r/programming, r/cpp, r/hardware are all wrong destinations. We are the intersection of three, which none of them accept. Hacker News will not pick us up — it wants world-changing products, not a roughly 680 KB CLI.

### 8.2 We must write all the docs ourselves

Ordinary tool users ask Stack Overflow. Neither-fish-fowl users will not find Stack Overflow answers, because nobody asks our kind of questions there. "How does BID combo5 extract the leading decimal digit", "how does DPD carry propagate across declets", "how does UInt256 schoolbook detect double carry" — search returns nothing.

So README, docs/, BASELINE, IMPLEMENTATION, UINT256, COMPLEXITY must all be 4000–8000 words each, pre-answering every possible question. That is why our docs are longer than the code. Not a style choice — the niche demands it, because no community will answer questions for us.

### 8.3 We cannot recruit ordinary contributors

The usual OSS funnel is: good first issue → fix bug → add feature. We have no good first issue, because the code is bit-level float arithmetic. A newcomer must already understand IEEE 754 decimals, schoolbook bignum, C++23, and MSYS2 before changing a single digit.

So this will be a single-maintainer project. That is a feature: the roadmap does not drift. No PR will drag us into building a GUI, GPU offload, MSVC support, WebAssembly, or Python bindings. All the "normal features" are deliberately absent.

### 8.4 Traffic follows a J-curve, not L-curve

L-type repos peak within 72 hours then decay. J-type repos sit near zero for 3–6 months then crawl upward. The first 3–5 months are a **structural dark period**, and the cause is not the content: search engines first have to discover the repo (crawl, sitemap submission), then build an index, then let the ranking settle — that pipeline alone takes months. A new repo has no backlinks and no stars, so its authority takes even longer to build. "It can be found" and "people do find it" are months apart; the dark period is a certainty, not bad luck. We chose J deliberately: scarce topics, long README, explosive repo name, no community promotion. The cost is watching GitHub Insights show single-digit Unique cloners for months and wondering if anyone is reading.

## 9. Will the niche ever be filled?

In theory yes. In practice hard, because filling it requires simultaneously:
- Real command of all of IEEE 754 (BID/DPD included)
- Willingness to write 17 software engines
- Willingness to reject MSVC, GUI, leaderboards
- Willingness to ship a roughly 680 KB CLI instead of a web app
- Willingness to spend half a year writing docs instead of marketing
- Willingness to maintain solo and not let PRs steer the roadmap

These are AND, not OR. Drop one and the niche stays empty.

Every project that looks similar already misses at least two:
- mpdecimal is a library, not a stress tool.
- Prime95 is stress, no decimals.
- boost.math is a math library, no hardware priority.
- float playground is education, no walks.
- llvm test-suite is a framework, not a standalone CLI.
- Intel RDFP is a vendor reference, no dual BID/DPD.

In 1–2 years nobody will contest this gap. In 5+ years someone may fork or imitate, but by then we are Google's top result and they trail behind.

## 10. One-line summary

We do not aim to be first in any mainstream category. We aim to be the only one in the gap between mainstream categories. The gap is empty not because nobody thought of it, but because the AND conditions above are rare. That is the whole meaning of "neither-fish-fowl".
