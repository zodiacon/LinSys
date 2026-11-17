#pragma once

#include <cstdint>
#include <string>
#include "Keys.h"

namespace LinSys {
	struct ThreadInfo {
		template<typename TProcessInfo, typename TThreadInfo>
		friend class ProcessManager;

		[[nodiscard]] const std::string& GetName() const { return m_Name; }
		[[nodiscard]] const std::string& GetProcessImageName() const { return m_ProcessName; }

		uint32_t Id{};
		uint32_t ProcessId{};
		ProcessOrThreadKey Key{};

		char State{};
		int64_t Priority{};
		int64_t Nice{};
		int32_t Processor{ -1 };
		uint64_t UserTime{};		// nanoseconds
		uint64_t KernelTime{};		// nanoseconds
		int32_t CPU{};				// 1/10000 of a percent of all processors, as WinSys's

	private:
		std::string m_Name;			// comm: the thread's name (pthread_setname_np), or the process's
		std::string m_ProcessName;
	};
}
