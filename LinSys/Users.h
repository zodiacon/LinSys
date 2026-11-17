#pragma once

#include <cstdint>
#include <string>

namespace LinSys {
	struct Users final {
		// the account's name (getpwuid_r), or the number as text if it has none
		[[nodiscard]] static std::string GetUserName(uint32_t uid);
	};
}
