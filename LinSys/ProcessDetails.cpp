#include "ProcessDetails.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <dirent.h>

namespace LinSys {
	namespace {
		std::string ProcessPath(const ProcessOrThreadKey& key, const std::string& procRoot) {
			return procRoot + "/" + std::to_string(key.Id);
		}

		// after reading: still the same process? (ESRCH if it's gone, or its id is another's now)
		int CheckStartTime(const ProcessOrThreadKey& key, const std::string& path) {
			auto text = ProcFs::ReadFile(path + "/stat");
			auto stat = text ? ProcFs::ParseStat(*text) : std::nullopt;
			return stat && static_cast<int64_t>(stat->StartTime) == key.Created ? 0 : ESRCH;
		}

		// a file that can't be read: why (ReadFile gives no errno, so the file is tried for its error)
		int ReadError(const std::string& path) {
			errno = 0;
			auto text = ProcFs::ReadFile(path);
			return text ? 0 : (errno ? errno : EACCES);
		}
	}

	int ProcessDetails::GetOpenFiles(const ProcessOrThreadKey& key, std::vector<OpenFile>& files, const std::string& procRoot) {
		files.clear();
		auto path = ProcessPath(key, procRoot);
		auto fdPath = path + "/fd";
		auto dir = ::opendir(fdPath.c_str());
		if (!dir)
			return errno;
		while (auto entry = ::readdir(dir)) {
			char* end;
			auto fd = std::strtol(entry->d_name, &end, 10);
			if (*end || end == entry->d_name)
				continue;
			// a file closed since the folder was read has no link
			auto target = ProcFs::ReadLink(fdPath + "/" + entry->d_name);
			if (!target)
				continue;
			OpenFile file{ static_cast<int>(fd), *target, ProcFs::ClassifyFdTarget(*target) };
			if (auto info = ProcFs::ReadFile(path + "/fdinfo/" + entry->d_name)) {
				auto fields = ProcFs::ParseFdInfo(*info);
				file.Position = fields.Position;
				file.Flags = fields.Flags;
			}
			files.push_back(std::move(file));
		}
		::closedir(dir);
		std::sort(files.begin(), files.end(), [](auto& a, auto& b) { return a.Fd < b.Fd; });
		if (auto error = CheckStartTime(key, path)) {
			files.clear();
			return error;
		}
		return 0;
	}

	int ProcessDetails::GetMemoryMaps(const ProcessOrThreadKey& key, std::vector<MemoryMap>& maps, const std::string& procRoot) {
		maps.clear();
		auto path = ProcessPath(key, procRoot);
		auto text = ProcFs::ReadFile(path + "/smaps");
		if (!text)
			text = ProcFs::ReadFile(path + "/maps");
		if (!text)
			return ReadError(path + "/maps");
		maps = ProcFs::ParseMaps(*text);
		// a process that can't be read through ptrace gets an empty file, not an error
		if (maps.empty() && !ProcFs::ReadFile(path + "/stat"))
			return ESRCH;
		if (auto error = CheckStartTime(key, path)) {
			maps.clear();
			return error;
		}
		return 0;
	}

	int ProcessDetails::GetEnvironment(const ProcessOrThreadKey& key, std::vector<std::pair<std::string, std::string>>& variables,
		const std::string& procRoot) {
		variables.clear();
		auto path = ProcessPath(key, procRoot);
		auto text = ProcFs::ReadFile(path + "/environ");
		if (!text)
			return ReadError(path + "/environ");
		variables = ProcFs::ParseEnviron(*text);
		if (auto error = CheckStartTime(key, path)) {
			variables.clear();
			return error;
		}
		return 0;
	}

	int ProcessDetails::GetCurrentDirectory(const ProcessOrThreadKey& key, std::string& path, const std::string& procRoot) {
		path.clear();
		auto processPath = ProcessPath(key, procRoot);
		errno = 0;
		auto target = ProcFs::ReadLink(processPath + "/cwd");
		if (!target)
			return errno ? errno : EACCES;
		if (auto error = CheckStartTime(key, processPath))
			return error;
		path = *target;
		return 0;
	}

	const std::string& SystemInfo::GetCpuModel() {
		static const std::string model = ProcFs::ParseCpuModel(ProcFs::ReadFile("/proc/cpuinfo").value_or(std::string()));
		return model;
	}
}
