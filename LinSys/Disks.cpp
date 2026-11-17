#include "Disks.h"
#include "ProcFs.h"

#include <algorithm>
#include <cstdlib>
#include <dirent.h>

namespace LinSys {
	namespace {
		std::string Trim(std::string text) {
			auto end = text.find_last_not_of(" \t\n");
			text.erase(end == std::string::npos ? 0 : end + 1);
			auto start = text.find_first_not_of(" \t");
			return start == std::string::npos ? std::string() : text.substr(start);
		}
	}

	std::vector<DiskInfo> Disks::List(const std::string& sysRoot) {
		std::vector<DiskInfo> disks;
		auto block = sysRoot + "/block";
		auto dir = ::opendir(block.c_str());
		if (!dir)
			return disks;
		while (auto entry = ::readdir(dir)) {
			std::string name = entry->d_name;
			if (name.starts_with('.') || name.starts_with("loop") || name.starts_with("ram") || name.starts_with("dm-"))
				continue;
			auto path = block + "/" + name;
			auto size = ProcFs::ReadFile(path + "/size");
			uint64_t sectors = size ? std::strtoull(size->c_str(), nullptr, 10) : 0;
			if (sectors == 0)
				continue;
			DiskInfo disk{ name, {}, sectors * ProcFs::SectorSize };
			if (auto model = ProcFs::ReadFile(path + "/device/model"))
				disk.Model = Trim(*model);
			disks.push_back(std::move(disk));
		}
		::closedir(dir);
		std::sort(disks.begin(), disks.end(), [](auto& a, auto& b) { return a.Name < b.Name; });
		return disks;
	}
}
