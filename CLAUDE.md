# CLAUDE.md

LinSys is the Linux counterpart of WinSys (github.com/zodiacon/WinSys, a submodule of WinSysLive): a C++23 static library of system queries for a Linux
version of WinSysLive, which will consume it as WinSysLive consumes WinSys. No UI here. Everything is in the `LinSys`
namespace.

## Build

CMake and Ninja, g++ 15 (Ubuntu in WSL on the development machine). From Windows:

```powershell
wsl -e bash -c "cd /mnt/c/Dev/linsys && cmake -S . -B build -G Ninja && cmake --build build"
wsl -e bash -c "cd /mnt/c/Dev/linsys && ./build/Tests"          # Tests Live / Tests -Live, as the app's
wsl -e bash -c "cd /mnt/c/Dev/linsys && ./build/linsyscon --tree"
```

- `LinSys`: the library. Consumers add this folder to the include path and include with the prefix:
  `<LinSys/ProcessManager.h>`.
- `Tests`: the app's runner (`TestFramework.h`: `TEST`, `CHECK`, `CHECK_EQ`), for `std::string`. Tests with "Live" in
  their names read the live system; the rest don't. `ctest` runs both.
- `linsyscon`: a console tool, as WinSysCon is WinSys's: a one-second sample, by CPU or as a tree.

## Architecture

- **Parsers apart from reading.** `ProcFs` parses `/proc` files' text (`ParseStat`, `ParseStatusUid`,
  `ParseCpuTimes`, `ParseMemInfo`, `ParseCommandLine`, `ImageNameFromCommandLine`) separately from reading them
  (`ReadFile`, `ReadLink`), so tests run them on fixed buffers. Keep new parsers that way, with a test, and see the
  test fail once before trusting it. The formats are in proc(5); stat's name can hold spaces and parentheses (it ends at
  the last `)`).
- **ProcessManager<TProcessInfo, TThreadInfo>** (`ProcessManager.h`, header-only) has WinSys's shape: `Update`,
  `UpdateWithThreads(pid)`, new and terminated processes and threads by `ProcessOrThreadKey` (id + start time in clock
  ticks), `BuildProcessTree`, CPU in 1/10000 of a percent of all processors. Differences:
  - The objects hold copies of their values, not pointers into a snapshot buffer, so there's no double buffering
    and no `Detach`: a terminated process stays valid as long as it's held.
  - `/proc` is read file by file, so a process can exit midway; it's left out and reported as terminated next time.
  - The parent id is updated on every update (Linux reparents orphans). The tree takes a process as a root when its
    parent started after it (the id was reused); start times are in ticks, so a child can share its parent's.
  - The constructor takes the folder to read (`/proc`) and the processor count, and `UpdateAt` the time: the tests
    build a fake `/proc` in a temporary folder (`FakeProc` in `ProcessManagerTests.cpp`) and check new, terminated,
    reused ids, CPU and threads deterministically.
  - Strings are UTF-8 `std::string`. Times are nanoseconds, sizes bytes.
- **Names.** `exe` can't be read for another user's process without root (or `CAP_SYS_PTRACE`), and `comm` is cut to
  15 characters, so the name then comes from `cmdline` (world-readable) when `comm` was cut: argv[0], or argv[1] for a
  script run by its interpreter. Kernel threads are told by `PF_KTHREAD` in stat's flags, not by their parent: in WSL
  pid 2 is `init-systemd`.
- `SystemStats`: overall and per-processor CPU (`/proc/stat`) and memory (`/proc/meminfo`) between two updates: the
  Performance tab's data.
- `Users::GetUserName`: `getpwuid_r`, cached.

## Conventions

WinSys's: tabs, `m_` members, `s_` statics, PascalCase methods, `[[nodiscard]]` getters, holders of static functions
as `struct X final`, errors as return values (`std::optional`, `bool`), no throwing. Unlike WinSys, the standard
library's exceptions aren't switched off.
