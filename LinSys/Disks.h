#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace LinSys {
	struct DiskInfo {
		std::string Name;	// as in /proc/diskstats: sda, nvme0n1
		std::string Model;	// device/model, trimmed; empty if there's none
		uint64_t Size{};	// bytes
	};

	struct Disks final {
		// The disks: /sys/block's devices (whole devices; partitions aren't there) but loop, ram and device-mapper
		// ones (views of the disks under them), and those of size 0 (a loop device with nothing attached, an empty
		// card reader). Sorted by name. sysRoot: "/sys", or a test's folder
		[[nodiscard]] static std::vector<DiskInfo> List(const std::string& sysRoot = "/sys");
	};
}
