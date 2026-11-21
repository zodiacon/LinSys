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
    build a fake `/proc` in a temporary folder (`Tests/FakeProc.h`, which LinSysLive's tests include too) and check new, terminated,
    reused ids, CPU and threads deterministically.
  - Strings are UTF-8 `std::string`. Times are nanoseconds, sizes bytes.
- **Names.** `exe` can't be read for another user's process without root (or `CAP_SYS_PTRACE`), and `comm` is cut to
  15 characters, so the name then comes from `cmdline` (world-readable) when `comm` was cut: argv[0], or argv[1] for a
  script run by its interpreter. Kernel threads are told by `PF_KTHREAD` in stat's flags, not by their parent: in WSL
  pid 2 is `init-systemd`.
- `SystemStats`: between two updates, overall and per-processor CPU (`/proc/stat`), memory (`/proc/meminfo`), each
  disk's read and write rates and active time (`/proc/diskstats`; sectors are always 512 bytes there) and each
  interface's receive and send rates (`/proc/net/dev`): the Performance tab's data. `UpdateFrom` takes the files' text,
  the time and the disk list, for tests; a counter that went back (a reset device) gives no rate rather than a huge one.
- `Disks::List`: the disks are `/sys/block`'s devices (partitions aren't there) but loop, ram and `dm-` ones and those of
  size 0, with `device/model` and their mount points: a mount's (`/proc/self/mountinfo`, whole file systems only:
  root "/") device leads to its disk through `/sys/dev/block/MAJ:MIN`, whose link ends in `block/<disk>[/<partition>]`
  (`DiskOfDeviceLink`). `SystemStats` lists them every 10 updates.
- `ProcessDetails`: a process's open files (`fd` links and `fdinfo`), memory maps (`smaps`, or `maps` without the
  sizes), environment and working directory, read on request, by key: the start time is checked after reading, so
  a reused id gives ESRCH, not another process's data. Errors are errno values (EACCES for another user's process
  without root). `fdinfo`'s flags are the kernel's: its `O_LARGEFILE` (0100000) is 0 in 64-bit headers.
  `SystemInfo::GetCpuModel`: `/proc/cpuinfo`'s model name (else Hardware, for ARM boards).
- `Users::GetUserName`: `getpwuid_r`, cached.
- `Process`: signals (Terminate, Kill, Suspend, Resume) by `ProcessOrThreadKey`, never a bare pid: the process is
  opened as a pidfd and its start time checked before the signal, so a reused id is left alone (ESRCH). Errors are
  errno values. `pidfd_open`/`pidfd_send_signal` are called by `syscall`: glibc 2.43's `<sys/pidfd.h>` lacks C linkage.
- `ServiceManager` (`Services.h`): systemd's services over D-Bus with sd-bus (libsystemd; `libsystemd-dev` to build,
  found through pkg-config), one connection, one thread. The constructor waits for the handshake
  (`sd_bus_get_unique_name`): sd-bus authenticates lazily, and the bus daemon drops a connection left unauthenticated
  for half a minute or so, so a first call made later got ENOTCONN. A call that gets ENOTCONN (nothing sent: the daemon
  restarted) reconnects and is made once more. `List`: the loaded units (`ListUnitsByPatterns`, *.service),
  the installed ones not loaded (`ListUnitFilesByPatterns`; templates left out) and the running ones' `MainPID`; 14 ms
  here, but the unit files take 200 ms (systemd reads its folders), so they're read only when asked (and after an
  action). `Start`, `Stop`, `Restart` queue a job; without root polkit decides (interactive authorization allowed:
  an agent may ask), else EACCES. Results are `BusResult`: an errno value and systemd's message. `Merge` is the
  pure part, tested; `linsyscon --services` lists them with the times.
- `Connections` (`Connections.h`): the sockets of `/proc/net/tcp`, `tcp6`, `udp` and `udp6` (this process's network
  namespace). Addresses are native-order 32-bit words in hex, so each word's bytes are copied as they are (the tests
  assume a little-endian machine, as the kernel's text does there). `SocketOwners` finds each socket's process from
  the `socket:[inode]` links in `/proc/[pid]/fd`: costly, so read again only when a socket appears that hasn't been
  looked for; one not found (another user's, without root) isn't looked for again while it lasts. A shared socket
  gets the first process found.

## Conventions

WinSys's: tabs, `m_` members, `s_` statics, PascalCase methods, `[[nodiscard]]` getters, holders of static functions
as `struct X final`, errors as return values (`std::optional`, `bool`), no throwing. Unlike WinSys, the standard
library's exceptions aren't switched off.
