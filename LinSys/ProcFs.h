#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

//
// Reading /proc. The parsers take the files' text and are kept apart from the reading, so tests can call them on
// fixed buffers; the formats are in proc(5).
//

namespace LinSys {
	// /proc/[pid]/stat and /proc/[pid]/task/[tid]/stat. Times are in clock ticks (ProcFs::ClockTicksPerSecond)
	struct StatFields {
		uint32_t Pid{};
		std::string Comm;			// the name, at most 15 characters; may hold spaces and parentheses
		char State{};				// R, S, D, Z, T, t, X, I...
		uint32_t ParentPid{};
		uint32_t Flags{};			// PF_* (ProcFs::KernelThreadFlag)
		uint64_t UserTime{};		// utime
		uint64_t KernelTime{};		// stime
		int64_t Priority{};
		int64_t Nice{};
		uint32_t ThreadCount{};
		uint64_t StartTime{};		// after boot
		uint64_t VirtualSize{};		// bytes
		uint64_t ResidentPages{};	// rss
		int32_t Processor{ -1 };	// the CPU it last ran on
	};

	// one cpu line of /proc/stat, in clock ticks
	struct CpuTimes {
		uint64_t User{}, Nice{}, System{}, Idle{}, IoWait{}, Irq{}, SoftIrq{}, Steal{};

		[[nodiscard]] uint64_t Busy() const { return User + Nice + System + Irq + SoftIrq + Steal; }
		[[nodiscard]] uint64_t Total() const { return Busy() + Idle + IoWait; }
	};

	// /proc/meminfo, in bytes
	struct MemoryInfo {
		uint64_t Total{}, Free{}, Available{}, Buffers{}, Cached{}, SwapTotal{}, SwapFree{};
	};

	// a line of /proc/diskstats: the device's counters since boot
	struct DiskCounters {
		std::string Name;
		uint64_t Reads{}, SectorsRead{}, Writes{}, SectorsWritten{};	// sectors of 512 bytes, whatever the device's
		uint64_t IoMilliseconds{};	// time with I/O in flight (the active time)
	};

	// a line of /proc/net/dev: the interface's counters since it came up
	struct NetworkCounters {
		std::string Name;
		uint64_t ReceivedBytes{}, ReceivedPackets{}, SentBytes{}, SentPackets{};
	};

	struct ProcFs final {
		static constexpr uint32_t KernelThreadFlag = 0x00200000;	// PF_KTHREAD
		static constexpr uint64_t SectorSize = 512;					// diskstats' and /sys/block's unit
		static constexpr size_t MaxCommLength = 15;					// TASK_COMM_LEN, less the NUL

		// the stat line; the name is between the first '(' and the last ')'
		[[nodiscard]] static std::optional<StatFields> ParseStat(std::string_view text);
		// the real user id: status's "Uid:" line
		[[nodiscard]] static std::optional<uint32_t> ParseStatusUid(std::string_view text);
		// /proc/stat's cpu lines: the total ("cpu") first, then each processor ("cpuN") in order
		[[nodiscard]] static std::vector<CpuTimes> ParseCpuTimes(std::string_view text);
		[[nodiscard]] static std::optional<MemoryInfo> ParseMemInfo(std::string_view text);
		// every device's line (partitions, loop and ram devices too: Disks::List says which are disks)
		[[nodiscard]] static std::vector<DiskCounters> ParseDiskStats(std::string_view text);
		// every interface after the two header lines (lo too)
		[[nodiscard]] static std::vector<NetworkCounters> ParseNetDev(std::string_view text);
		// cmdline's arguments, separated by NULs, joined by spaces (empty for a kernel thread)
		[[nodiscard]] static std::string ParseCommandLine(std::string_view text);
		// The name for a process whose exe can't be read (another user's, without root): comm, unless it was cut to
		// 15 characters, in which case the file name of argv[0] or argv[1] that starts with it. cmdline: the raw
		// file, its arguments separated by NULs
		[[nodiscard]] static std::string ImageNameFromCommandLine(std::string_view cmdline, std::string_view comm);

		// the whole file (files in /proc report a size of 0, so this reads until the end); nullopt if it can't be opened
		[[nodiscard]] static std::optional<std::string> ReadFile(const std::string& path);
		// a symbolic link's target (as /proc/[pid]/exe); nullopt if it can't be read (another user's process)
		[[nodiscard]] static std::optional<std::string> ReadLink(const std::string& path);

		[[nodiscard]] static uint64_t ClockTicksPerSecond();
		[[nodiscard]] static uint64_t PageSize();
		[[nodiscard]] static uint32_t ProcessorCount();
	};
}
