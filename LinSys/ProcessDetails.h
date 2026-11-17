#pragma once

#include <string>
#include <utility>
#include <vector>
#include "Keys.h"
#include "ProcFs.h"

namespace LinSys {
	struct OpenFile {
		int Fd{};
		std::string Target;		// the link: a path, "socket:[123]", "pipe:[45]", "anon_inode:[eventfd]"
		FileKind Kind{};
		uint64_t Position{};
		uint32_t Flags{};		// O_*
	};

	// A process's open files, memory and environment, read from /proc/[pid] on request (the Processes tab's lower
	// pane). Each takes the process's key and checks its start time after reading, so a reused id gives ESRCH, not
	// another process's data. Each returns 0 or an errno value: EACCES for another user's process without root
	// (CAP_SYS_PTRACE), ESRCH when it's gone
	struct ProcessDetails final {
		// by fd number
		[[nodiscard]] static int GetOpenFiles(const ProcessOrThreadKey& key, std::vector<OpenFile>& files,
			const std::string& procRoot = "/proc");
		// with the regions' sizes (smaps), or without them (maps) if smaps can't be read
		[[nodiscard]] static int GetMemoryMaps(const ProcessOrThreadKey& key, std::vector<MemoryMap>& maps,
			const std::string& procRoot = "/proc");
		// in the process's order
		[[nodiscard]] static int GetEnvironment(const ProcessOrThreadKey& key, std::vector<std::pair<std::string, std::string>>& variables,
			const std::string& procRoot = "/proc");
		// the working directory (cwd's link)
		[[nodiscard]] static int GetCurrentDirectory(const ProcessOrThreadKey& key, std::string& path,
			const std::string& procRoot = "/proc");
	};

	struct SystemInfo final {
		// the processor's name (/proc/cpuinfo's model name), read once; empty if there's none
		[[nodiscard]] static const std::string& GetCpuModel();
	};
}
