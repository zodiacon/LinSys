#include "Disks.h"
#include "ProcFs.h"

#include <algorithm>
#include <cstdlib>
#include <dirent.h>
#include <map>

namespace LinSys {
	namespace {
		std::string Trim(std::string text) {
			auto end = text.find_last_not_of(" \t\n");
			text.erase(end == std::string::npos ? 0 : end + 1);
			auto start = text.find_first_not_of(" \t");
			return start == std::string::npos ? std::string() : text.substr(start);
		}
	}

	std::string Disks::DiskOfDeviceLink(const std::string& link) {
		// "../../devices/pci0000:00/.../block/sda/sda1", "../../devices/virtual/block/dm-0"
		auto at = link.rfind("/block/");
		if (at == std::string::npos)
			return {};
		auto disk = link.substr(at + 7);
		return disk.substr(0, disk.find('/'));
	}

	std::vector<DiskInfo> Disks::List(const std::string& sysRoot, const std::string& procRoot) {
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
			DiskInfo disk{ name, {}, sectors * ProcFs::SectorSize, {} };
			if (auto model = ProcFs::ReadFile(path + "/device/model"))
				disk.Model = Trim(*model);
			disks.push_back(std::move(disk));
		}
		::closedir(dir);
		std::sort(disks.begin(), disks.end(), [](auto& a, auto& b) { return a.Name < b.Name; });

		// the mounts of whole file systems (root "/") on a device, by disk
		std::map<std::string, std::vector<std::string>> mountPoints;
		std::map<std::pair<uint32_t, uint32_t>, std::string> diskOfDevice;
		for (auto& mount : ProcFs::ParseMountInfo(ProcFs::ReadFile(procRoot + "/self/mountinfo").value_or(std::string()))) {
			if (mount.Major == 0 || mount.Root != "/")
				continue;
			auto device = std::make_pair(mount.Major, mount.Minor);
			auto it = diskOfDevice.find(device);
			if (it == diskOfDevice.end()) {
				auto link = ProcFs::ReadLink(sysRoot + "/dev/block/" + std::to_string(mount.Major) + ":" + std::to_string(mount.Minor));
				it = diskOfDevice.emplace(device, link ? DiskOfDeviceLink(*link) : std::string()).first;
			}
			if (!it->second.empty())
				mountPoints[it->second].push_back(mount.MountPoint);
		}
		for (auto& disk : disks) {
			auto& points = mountPoints[disk.Name];
			std::sort(points.begin(), points.end(), [](auto& a, auto& b) { return a.size() != b.size() ? a.size() < b.size() : a < b; });
			points.erase(std::unique(points.begin(), points.end()), points.end());
			disk.MountPoints = std::move(points);
		}
		return disks;
	}
}
