#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "Keys.h"
#include "ProcFs.h"

namespace LinSys {
	struct ThreadInfo;

	// A process in a ProcessManager snapshot. Unlike WinSys's, it holds copies of its values, not pointers into a
	// buffer, so it stays valid after the process exits and needs no Detach
	struct ProcessInfo {
		template<typename TProcessInfo, typename TThreadInfo>
		friend class ProcessManager;

		virtual ~ProcessInfo() = default;

		// the executable's file name; when exe can't be read (kernel threads, another user's process without root),
		// comm, or the command line's name when comm was cut (ProcFs::ImageNameFromCommandLine)
		[[nodiscard]] const std::string& GetImageName() const { return m_ImageName; }
		// /proc/[pid]/exe; empty if it can't be read
		[[nodiscard]] const std::string& GetImagePath() const { return m_ImagePath; }
		[[nodiscard]] const std::string& GetComm() const { return m_Comm; }
		// read on first use (empty for a kernel thread or once the process is gone)
		[[nodiscard]] const std::string& GetCommandLine() const;
		[[nodiscard]] const std::string& GetUserName() const;
		[[nodiscard]] const std::vector<std::shared_ptr<ThreadInfo>>& GetThreads() const { return m_Threads; }
		// a kernel thread (kthreadd and its children) has no user-space image (not by its parent: in WSL, pid 2 is
		// init-systemd and there are none)
		[[nodiscard]] bool IsKernelThread() const { return (Flags & ProcFs::KernelThreadFlag) != 0; }

		uint32_t Id{};
		uint32_t ParentId{};
		ProcessOrThreadKey Key{};

		char State{};
		uint32_t Flags{};			// PF_*
		int64_t Priority{};
		int64_t Nice{};
		uint32_t ThreadCount{};
		uint32_t Uid{};
		int32_t Processor{ -1 };
		uint64_t UserTime{};		// nanoseconds
		uint64_t KernelTime{};		// nanoseconds
		uint64_t StartTime{};		// nanoseconds after boot
		uint64_t VirtualSize{};		// bytes
		uint64_t ResidentSize{};	// bytes
		int32_t CPU{};				// 1/10000 of a percent of all processors, as WinSys's
		int32_t KernelCPU{};

	private:
		std::string m_ProcRoot;		// "/proc", or a test's folder
		std::string m_ImageName;
		std::string m_ImagePath;
		std::string m_Comm;
		mutable std::string m_CommandLine;
		mutable std::string m_UserName;
		mutable bool m_CommandLineRead{ false };
		std::vector<std::shared_ptr<ThreadInfo>> m_Threads;
	};
}
