#pragma once

#include <cstdint>
#include <functional>

namespace LinSys {
	// A process or thread is its id and its start time: ids are reused, the pair isn't. Created is the start time in
	// clock ticks after boot (/proc/[pid]/stat's starttime), as WinSys's is the CreateTime
	struct ProcessOrThreadKey {
		int64_t Created;
		uint32_t Id;

		bool operator==(const ProcessOrThreadKey& other) const = default;
	};
}

template<>
struct std::hash<LinSys::ProcessOrThreadKey> {
	size_t operator()(const LinSys::ProcessOrThreadKey& key) const noexcept {
		return std::hash<int64_t>()(key.Created) ^ (static_cast<size_t>(key.Id) << 1);
	}
};
