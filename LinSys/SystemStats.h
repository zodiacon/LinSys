#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "ProcFs.h"

namespace LinSys {
	// The system's CPU use (overall and per processor) and memory, between two updates: what the Performance tab
	// graphs. CPU values are in 1/10000 of a percent, as ProcessInfo::CPU
	class SystemStats {
	public:
		explicit SystemStats(std::string procRoot = "/proc") : m_ProcRoot(std::move(procRoot)) {}

		// reads /proc/stat and /proc/meminfo; false if either can't be read or parsed (the previous values stay)
		bool Update();
		// the same on the files' text (tests)
		bool UpdateFrom(std::string_view stat, std::string_view meminfo);

		// 0 until the second update
		[[nodiscard]] int32_t GetCpu() const { return m_Cpu; }
		[[nodiscard]] int32_t GetKernelCpu() const { return m_KernelCpu; }
		[[nodiscard]] const std::vector<int32_t>& GetProcessorCpu() const { return m_ProcessorCpu; }
		[[nodiscard]] const MemoryInfo& GetMemory() const { return m_Memory; }

	private:
		std::string m_ProcRoot;
		std::vector<CpuTimes> m_Prev;
		std::vector<int32_t> m_ProcessorCpu;
		int32_t m_Cpu{};
		int32_t m_KernelCpu{};
		MemoryInfo m_Memory;
	};
}
