#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "Disks.h"
#include "ProcFs.h"

namespace LinSys {
	struct DiskRate {
		DiskInfo Disk;
		uint64_t ReadRate{}, WriteRate{};			// bytes per second
		int32_t Active{};							// time with I/O in flight, in 1/10000 of a percent
		uint64_t TotalRead{}, TotalWritten{};		// bytes since boot
	};

	struct NetworkRate {
		std::string Name;
		uint64_t ReceiveRate{}, SendRate{};			// bytes per second
		uint64_t TotalReceived{}, TotalSent{};		// bytes since the interface came up
	};

	// The system's CPU use (overall and per processor), memory, and each disk's and network interface's rates,
	// between two updates: what the Performance tab graphs. CPU values are in 1/10000 of a percent, as
	// ProcessInfo::CPU. Rates are 0 until the second update (and for a device that just appeared)
	class SystemStats {
	public:
		// the files' text, for UpdateFrom
		struct Sources {
			std::string_view Stat, MemInfo, DiskStats, NetDev;
		};

		explicit SystemStats(std::string procRoot = "/proc", std::string sysRoot = "/sys")
			: m_ProcRoot(std::move(procRoot)), m_SysRoot(std::move(sysRoot)) {}

		// reads /proc/stat, meminfo, diskstats and net/dev (and lists the disks now and then); false if stat or
		// meminfo can't be read or parsed (the previous values stay)
		bool Update();
		// the same on the files' text, at a time in nanoseconds on a clock that only goes forward (tests)
		bool UpdateFrom(const Sources& sources, int64_t now, const std::vector<DiskInfo>& disks);

		[[nodiscard]] int32_t GetCpu() const { return m_Cpu; }
		[[nodiscard]] int32_t GetKernelCpu() const { return m_KernelCpu; }
		[[nodiscard]] const std::vector<int32_t>& GetProcessorCpu() const { return m_ProcessorCpu; }
		[[nodiscard]] const MemoryInfo& GetMemory() const { return m_Memory; }
		// the disks (Disks::List), by name
		[[nodiscard]] const std::vector<DiskRate>& GetDisks() const { return m_Disks; }
		// the interfaces in net/dev's order, lo included
		[[nodiscard]] const std::vector<NetworkRate>& GetNetworks() const { return m_Networks; }

	private:
		std::string m_ProcRoot, m_SysRoot;
		int64_t m_PrevTime{};
		std::vector<CpuTimes> m_Prev;
		std::unordered_map<std::string, DiskCounters> m_PrevDisks;
		std::unordered_map<std::string, NetworkCounters> m_PrevNetworks;
		std::vector<DiskInfo> m_DiskList;
		int m_UpdatesSinceList{};
		std::vector<int32_t> m_ProcessorCpu;
		int32_t m_Cpu{};
		int32_t m_KernelCpu{};
		MemoryInfo m_Memory;
		std::vector<DiskRate> m_Disks;
		std::vector<NetworkRate> m_Networks;
	};
}
