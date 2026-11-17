#include "Users.h"

#include <mutex>
#include <pwd.h>
#include <unordered_map>
#include <vector>

namespace LinSys {
	std::string Users::GetUserName(uint32_t uid) {
		// names are looked up for every process, and there are few accounts
		static std::mutex lock;
		static std::unordered_map<uint32_t, std::string> cache;
		{
			std::lock_guard guard(lock);
			if (auto it = cache.find(uid); it != cache.end())
				return it->second;
		}

		std::string name;
		passwd pwd{}, *result = nullptr;
		std::vector<char> buffer(16384);
		if (::getpwuid_r(uid, &pwd, buffer.data(), buffer.size(), &result) == 0 && result)
			name = result->pw_name;
		else
			name = std::to_string(uid);

		std::lock_guard guard(lock);
		cache.emplace(uid, name);
		return name;
	}
}
