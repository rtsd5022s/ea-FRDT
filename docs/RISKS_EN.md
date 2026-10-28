# Consequences and Costs of Using This Tool

This document does not exaggerate, and it does not downplay. It says one thing plainly: ea-FRDT is a tool that intentionally pushes the CPU to its limit. The moment you press Enter, you own everything that happens to that machine. This is not boilerplate disclaimers; it is a concrete list of costs that have actually happened and can be reproduced.

## 1. Hardware Costs

### 1.1 Electricity

A CPU at full load draws 4 to 8 times its idle power. An i7-6700 idles at about 6 watts and pulls 91 watts under load. That does not count motherboard, memory, and fan overhead. A desktop under load uses roughly 0.1 kWh per hour. Run it for an 8-hour night, that is 0.8 kWh. At residential rates that is a few cents, trivial. But on a server, a dual-socket 2U box pulls 600W+ under load, that is 14 kWh per day, 420 kWh per month, and at commercial rates of $0.15 per kWh that is $63 per month. For a full year, $750 in electricity alone, not counting cooling and rack space.

Laptops are worse. A gaming laptop under combined CPU+GPU load pulls 180W from the adapter, which gets hot to the touch. On battery, the battery drains fast, and because CPU turbo is itself throttled in battery mode, the measured speed is slower than on AC, so you think your code is slow when the battery simply cannot supply the current.

### 1.2 Cooling and noise

The heat from a loaded CPU has to go somewhere. A stock Intel air cooler on an i7-6700 spins its fan above 2500 RPM under load, producing 40+ dB of noise. In a quiet room that is like having a vacuum cleaner running in the next room. A water cooler is quieter, but the pump itself makes noise, and a leak is its own risk.

Laptops are worse. Laptop cooling is designed for "occasional load," not "sustained load for hours." After 10 minutes the heatsink is hot, the fan spins up to 5000 RPM, and the exhaust air is hot to the touch. After 30+ minutes the keyboard deck can reach 45C, too hot to rest your wrists on. Sustained load dries out thermal paste faster, ages cooling pads, and shortens the usable life of the laptop's cooling solution.

### 1.3 Throttling and hardware life

CPUs have thermal protection. Above 95C they start downclocking, dropping from 4.0 GHz to 3.5, 3.0, 2.5 GHz until temperature returns to a safe range. This means that after half an hour of stress testing, the steps/sec you measure is not peak, it is post-throttle. You think the code slowed down; the CPU is protecting itself.

The longer-term cost is hardware lifespan. CPU design lifetime assumes "typical workload," which means most of the time idle or light, with occasional bursts. If you run the CPU at 100% 24/7, electromigration accelerates. The CPU will not die tomorrow, but five years from now your CPU may fail earlier than one used normally. This cost is invisible, but it is happening.

Hard drives are not immune. Stress testing itself barely touches disk, but Windows does background updates and antivirus scans under CPU load, which generate disk I/O. If your system drive is an SSD, sustained load increases write volume and consumes TBW. Mechanical drives have motor and head wear from long runs under vibration.

### 1.4 Power supply

The power supply is the most overlooked component. A loaded CPU draws highly fluctuating current; turbo spikes can draw twice the average. A cheap no-name 500W supply under this fluctuation can deliver unstable voltage, causing hangs and reboots, or worse, damaging other hardware. If you run an old supply (5+ years), the capacitors have aged and ripple under load may exceed spec. Before stress testing, confirm your supply has 30% headroom over the rated load, not "just enough."

## 2. Operating System Costs

### 2.1 System lag

This is the most immediate. On an 8-core CPU running 8 workers, each pinned to a core and bumped to realtime priority (THREAD_PRIORITY_TIME_CRITICAL). The Windows scheduler prioritizes high-priority threads, and everything else (Explorer, antivirus, IME, browser) gets squeezed onto whatever cores are left. If all 8 cores are taken, other processes run in the gaps of time slices.

In practice: mouse drag has lag, window switching takes seconds, even typing stutters. When you want to press Ctrl+C to stop the tool, the terminal may take a second to respond. This is not a bug; it is the expected behavior of pinning plus realtime priority. If you want to use the machine while it runs, pin fewer cores: on a 4-core CPU use `-j 1-2` and leave two cores for the OS.

### 2.2 Background tasks starved

Windows Update, Defender full scans, OneDrive sync, browser update checks: these background tasks get starved under CPU load. They do not stop running, they simply cannot get CPU time. After a night of stress testing you find Windows Update still downloading yesterday's patch, and last night's OneDrive sync never completed. The system is not broken; background tasks did not win the CPU lottery.

Antivirus may start scanning the tool's temporary files under load (though this tool barely writes anything), and the scan itself eats CPU, competing with the workers. It is worth temporarily disabling real-time protection during a run and re-enabling it after.

### 2.3 Watchdog and system protection

Windows has watchdog mechanisms. If a critical system process (like the window manager dwm.exe) does not get scheduled for too long, the system triggers a watchdog reboot. In extreme cases (e.g., running 2 realtime workers on a 2-core VM), dwm can starve and you get a blue screen. This is not common, but on small core counts be careful.

Linux has similar. systemd-udevd and watchdogd that do not respond in time under CPU load trigger a hardware watchdog reboot. Be careful running stress tests in containers: cgroup CPU quotas and core pinning are different things, and pinning to cores outside the container produces weird behavior.

### 2.4 Network and peripherals

Under CPU load, the network stack's interrupt handling capacity drops. When you SSH into the machine during a run, SSH responses lag and may time out. Network share (SMB/NFS) latency goes up. Printers and USB devices respond slowly. This is not caused directly by the tool; it is the cascading effect of the whole system's responsiveness dropping when the CPU is saturated.

## 3. Data and Work Costs

### 3.1 Unsaved work

If you are running a stress test with an unsaved Word document, uncommitted code, or an unfinished video render in another window, system lag can make those apps appear to be "not responding." You think they crashed, you kill the process, and your work is gone. The stress test does not directly break your apps, but system lag induces you to make bad decisions.

Before starting: save all work, close apps you do not need. Common sense, but people forget.

### 3.2 Crashes and reboots

In extreme cases the system bluescreens or reboots automatically. Unsaved data is lost. If it is an SSD, a software reboot (not a power cut) should not lose data, but the filesystem may be inconsistent and the next boot runs a disk check. The check itself reads the whole disk; SSDs handle it fine, mechanical drives take a long time.

### 3.3 Multi-tenant environments

If you run this tool on a shared server, you saturate the CPU and other users' processes slow down. It is not your machine; you do not have the right. Talk to the administrator before running on a shared box, or run it in your own container (with a CPU quota). Do not run this on production; if you do, that is an incident.

## 4. Extra Costs of DPD Formats

DPD formats (dpd32/dpd64/dpd128) are pure software decimal-array arithmetic: per-digit multiplication, carry, rounding, plus DPD triplet codec lookups. No hardware acceleration. Measured: DPD128 is about 3x faster than BID Dec128 (because BID uses UInt256 schoolbook big-integer multiply, which has high per-step overhead), but DPD cannot use the hardware FPU, so weak CPUs stay loaded longer.

What does this mean? If you include DPD formats in `-m all`, that worker stays loaded longer than pure binary formats. Other workers finish their chain in seconds; the DPD worker takes longer. You think the tool hung, but it is faithfully computing DPD. Worse: the DPD worker occupies the CPU longer, meaning your machine stays loaded longer, and cooling, electricity, and noise costs all double.

DPD prints a yellow warning before starting, waiting 10 seconds for `y`. This warning is not decoration; it is meant to make you think: are you sure you want the machine loaded for another half hour just for a DPD run?

## 5. Infinite Loops and Timeouts

`-l -1` is infinite loop. If you do not pair it with `-t`, the tool runs forever. On a local machine it is fine, you press Ctrl+C. But if you run it over SSH on a remote box, and the SSH session drops (network blip, laptop lid closes), the SSH process dies but the tool process does not; it becomes an orphan and keeps eating CPU. The next time you SSH in, you see 100% CPU and `ps` to find the orphan.

Worse: if you used nohup or systemd-run to background it, it keeps running as a service until you reboot. This is not hypothetical; people have done exactly this.

**Rule: always pair `-l -1` with `-t`.** The timeout fires and the tool exits on its own; you do not have to babysit it.

## 6. Costs of Measurement Itself

The point of stress testing is measurement. But measurement itself introduces noise:

- **Thermal noise**: after a few minutes the CPU heats up and throttles, speed drops. You measure not "peak speed" but "post-thermal-equilibrium speed."
- **Scheduling noise**: other processes (antivirus, updates, sync) steal CPU in the background, so measured speed fluctuates. For a clean environment, disable background services.
- **Frequency variation**: CPU turbo is not constant. 4.0 GHz light, 3.5 GHz loaded, 3.0 GHz hot. The steps/sec you measure is an average over the whole run, not an instantaneous snapshot.
- **Cache warmth**: when switching formats, the CPU cache is cold and the first few steps are slow. The speed curve you see is "ramp-up -> steady -> throttle," not a flat line.

This noise is not something the tool can eliminate; it is physics. Your job is to understand the noise and not treat the measured numbers as absolute truth.

## 7. Psychological and Time Costs

Stress testing looks like "just let it run," but in practice you have to watch it:

- Watch the monitor to confirm it is running normally, not hung.
- Watch temperatures (if you have monitoring software) to confirm nothing is at a dangerous value.
- Listen to fan noise to catch unusual rattles.
- Have Ctrl+C ready to stop at any moment.

This is not a "set and forget" task; it is a "run and watch" task. The time you spend babysitting the run could have been spent writing code, reading papers, or resting. A 10-second run is nothing; a 2-hour run means you spent 2 hours waiting.

## 8. Legal and Liability

This tool is MIT licensed; the author is not responsible for your use. If you burn up a CPU, crash your system, lose data, violate company policy, or rack up an electricity bill, that is on you. MIT means "code provided as-is, no warranty."

Specifically:

- You run this on a company machine, violate IT policy, and get fired. Your problem.
- You run this on a server, impact other workloads, and cause an outage. Your problem.
- Your hardware fails during a run, you file a warranty claim, and the manufacturer detects "non-typical sustained high load" and rejects the claim. Your problem.
- You lose data because of a run and had no backup. Your problem.

This is not fearmongering; it is standard MIT terms. Open source tools do not assume the consequences of your use.

## 9. When You Should Run This Tool

After all these costs, this is not a "do not use it" message. It is a "think before you run" message:

- You are verifying a float format's behavior and need to know how many steps from 1.0 to overflow. Run it.
- You are debugging a compiler and want to see the bit pattern of x87 at a specific step. Run it.
- You are stress-testing system stability and want to see whether it crashes under load. Run it, but on a test machine.
- You just want to see what maxing out the CPU feels like. Run it, but stop after 10 seconds, not after a day.

Do not run it on production. Do not run it on a machine where you do not know what will happen. Do not run it on a machine you do not have permission to use.

## 10. After the Run

After the stress test, do these things:

1. Confirm the process has exited (check Task Manager, CPU usage back to normal).
2. If it ran for a long time, let the machine idle for 10 minutes to cool.
3. Save your measurement data (screenshot or redirect to a file).
4. On a laptop, plug in the charger and let the battery recover.
5. If anything seems off (bluescreen, reboot, missing files), check the disk and your backups.

This tool is a scalpel, not a hammer. Used in the right place it lets you see the edges of float formats clearly. Used in the wrong place it is just an electricity-wasting toy.
