#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace LinSys {
	struct DiskInfo {
		std::string Name;	// as in /proc/diskstats: sda, nvme0n1
		std::string Model;	// device/model, trimmed; empty if there's none
		uint64_t Size{};	// bytes
		// where the disk or its partitions are mounted, shortest first ("/", "/home"); bind mounts of folders
		// left out
		std::vector<std::string> MountPoints;
	};

	struct Disks final {
		// The disks: /sys/block's devices (whole devices; partitions aren't there) but loop, ram and device-mapper
		// ones (views of the disks under them), and those of size 0 (a loop device with nothing attached, an empty
		// card reader). Sorted by name. A mount's device leads to its disk through /sys/dev/block/MAJ:MIN, whose link
		// ends in .../block/<disk>[/<partition>]. sysRoot and procRoot: "/sys" and "/proc", or a test's folders
		[[nodiscard]] static std::vector<DiskInfo> List(const std::string& sysRoot = "/sys", const std::string& procRoot = "/proc");
		// the disk a device link names: the component after the last "block" ("" if none)
		[[nodiscard]] static std::string DiskOfDeviceLink(const std::string& link);
	};
}
