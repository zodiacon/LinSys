#include "ProcFs.h"

#include <algorithm>
#include <charconv>
#include <fcntl.h>
#include <unistd.h>

namespace LinSys {
	namespace {
		// the next space-separated field, or an empty view at the end
		std::string_view NextField(std::string_view& text) {
			auto start = text.find_first_not_of(" \t\n");
			if (start == std::string_view::npos) {
				text = {};
				return {};
			}
			text.remove_prefix(start);
			auto end = text.find_first_of(" \t\n");
			auto field = text.substr(0, end);
			text.remove_prefix(end == std::string_view::npos ? text.size() : end);
			return field;
		}

		template<typename T>
		bool ToNumber(std::string_view text, T& value) {
			auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
			return ec == std::errc() && ptr == text.data() + text.size();
		}

		std::string_view NextLine(std::string_view& text) {
			auto end = text.find('\n');
			auto line = text.substr(0, end);
			text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
			return line;
		}
	}

	std::optional<StatFields> ProcFs::ParseStat(std::string_view text) {
		auto open = text.find('(');
		auto close = text.rfind(')');
		if (open == std::string_view::npos || close == std::string_view::npos || close < open)
			return std::nullopt;

		StatFields stat;
		auto pid = text.substr(0, open);
		while (!pid.empty() && pid.back() == ' ')
			pid.remove_suffix(1);
		if (!ToNumber(pid, stat.Pid))
			return std::nullopt;
		stat.Comm = std::string(text.substr(open + 1, close - open - 1));

		// the fields after the name, numbered as in proc(5): state is 3
		auto rest = text.substr(close + 1);
		int index = 3;
		for (auto field = NextField(rest); !field.empty(); field = NextField(rest), index++) {
			bool ok = true;
			switch (index) {
				case 3: stat.State = field[0]; break;
				case 4: ok = ToNumber(field, stat.ParentPid); break;
				case 9: ok = ToNumber(field, stat.Flags); break;
				case 14: ok = ToNumber(field, stat.UserTime); break;
				case 15: ok = ToNumber(field, stat.KernelTime); break;
				case 18: ok = ToNumber(field, stat.Priority); break;
				case 19: ok = ToNumber(field, stat.Nice); break;
				case 20: ok = ToNumber(field, stat.ThreadCount); break;
				case 22: ok = ToNumber(field, stat.StartTime); break;
				case 23: ok = ToNumber(field, stat.VirtualSize); break;
				case 24: ok = ToNumber(field, stat.ResidentPages); break;
				case 39: ok = ToNumber(field, stat.Processor); break;
			}
			if (!ok)
				return std::nullopt;
		}
		// starttime is the last field every kernel has had that we need
		if (index <= 24)
			return std::nullopt;
		return stat;
	}

	std::optional<uint32_t> ProcFs::ParseStatusUid(std::string_view text) {
		while (!text.empty()) {
			auto line = NextLine(text);
			if (line.starts_with("Uid:")) {
				line.remove_prefix(4);
				uint32_t uid;
				if (ToNumber(NextField(line), uid))
					return uid;
				return std::nullopt;
			}
		}
		return std::nullopt;
	}

	std::vector<CpuTimes> ProcFs::ParseCpuTimes(std::string_view text) {
		std::vector<CpuTimes> cpus;
		while (!text.empty()) {
			auto line = NextLine(text);
			if (!line.starts_with("cpu"))
				continue;
			NextField(line);
			CpuTimes times;
			uint64_t* values[] = { &times.User, &times.Nice, &times.System, &times.Idle, &times.IoWait,
				&times.Irq, &times.SoftIrq, &times.Steal };
			// older kernels have fewer columns; the ones missing stay 0 (guest times, after steal, are in user and nice)
			for (auto value : values) {
				auto field = NextField(line);
				if (field.empty())
					break;
				if (!ToNumber(field, *value))
					return {};
			}
			cpus.push_back(times);
		}
		return cpus;
	}

	std::optional<MemoryInfo> ProcFs::ParseMemInfo(std::string_view text) {
		MemoryInfo info;
		struct Entry {
			std::string_view Name;
			uint64_t* Value;
		} entries[] = {
			{ "MemTotal:", &info.Total }, { "MemFree:", &info.Free }, { "MemAvailable:", &info.Available },
			{ "Buffers:", &info.Buffers }, { "Cached:", &info.Cached },
			{ "SwapTotal:", &info.SwapTotal }, { "SwapFree:", &info.SwapFree },
		};
		bool total = false;
		while (!text.empty()) {
			auto line = NextLine(text);
			auto name = NextField(line);
			for (auto& entry : entries) {
				if (name == entry.Name) {
					uint64_t kb;
					if (!ToNumber(NextField(line), kb))
						return std::nullopt;
					*entry.Value = kb * 1024;
					total |= entry.Value == &info.Total;
					break;
				}
			}
		}
		if (!total)
			return std::nullopt;
		return info;
	}

	std::vector<DiskCounters> ProcFs::ParseDiskStats(std::string_view text) {
		std::vector<DiskCounters> disks;
		while (!text.empty()) {
			auto line = NextLine(text);
			NextField(line);	// major
			NextField(line);	// minor
			auto name = NextField(line);
			if (name.empty())
				continue;
			// the fields as Documentation/admin-guide/iostats.rst numbers them, from 1
			uint64_t fields[10]{};
			bool ok = true;
			for (auto& field : fields)
				ok = ok && ToNumber(NextField(line), field);
			if (!ok)
				continue;
			disks.push_back({ std::string(name), fields[0], fields[2], fields[4], fields[6], fields[9] });
		}
		return disks;
	}

	std::vector<NetworkCounters> ProcFs::ParseNetDev(std::string_view text) {
		std::vector<NetworkCounters> interfaces;
		while (!text.empty()) {
			auto line = NextLine(text);
			// "  eth0: 1234 ..."; old kernels put no space after the colon
			auto colon = line.find(':');
			if (colon == std::string_view::npos)
				continue;
			auto name = line.substr(0, colon);
			name.remove_prefix(std::min(name.find_first_not_of(' '), name.size()));
			if (name.empty() || name.find('|') != std::string_view::npos)
				continue;	// a header
			line.remove_prefix(colon + 1);
			// receive: bytes packets errs drop fifo frame compressed multicast; then transmit: bytes packets...
			uint64_t fields[10]{};
			bool ok = true;
			for (auto& field : fields)
				ok = ok && ToNumber(NextField(line), field);
			if (!ok)
				continue;
			interfaces.push_back({ std::string(name), fields[0], fields[1], fields[8], fields[9] });
		}
		return interfaces;
	}

	std::string ProcFs::ParseCommandLine(std::string_view text) {
		while (!text.empty() && text.back() == '\0')
			text.remove_suffix(1);
		std::string result(text);
		for (auto& c : result)
			if (c == '\0')
				c = ' ';
		return result;
	}

	std::string ProcFs::ParseCpuModel(std::string_view cpuinfo) {
		std::string_view fallback;
		while (!cpuinfo.empty()) {
			auto line = NextLine(cpuinfo);
			auto colon = line.find(':');
			if (colon == std::string_view::npos)
				continue;
			auto key = line.substr(0, colon);
			while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
				key.remove_suffix(1);
			auto value = line.substr(colon + 1);
			value.remove_prefix(std::min(value.find_first_not_of(" \t"), value.size()));
			if (key == "model name")
				return std::string(value);
			if ((key == "Hardware" || key == "cpu model") && fallback.empty())
				fallback = value;
		}
		return std::string(fallback);
	}

	namespace {
		// mountinfo writes a space, tab, newline and backslash as \040, \011, \012 and \134
		std::string Unescape(std::string_view text) {
			std::string result;
			result.reserve(text.size());
			for (size_t i = 0; i < text.size(); i++) {
				if (text[i] == '\\' && i + 3 < text.size()) {
					auto digits = text.substr(i + 1, 3);
					if (std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '7'; })) {
						result += static_cast<char>((digits[0] - '0') * 64 + (digits[1] - '0') * 8 + (digits[2] - '0'));
						i += 3;
						continue;
					}
				}
				result += text[i];
			}
			return result;
		}

		bool ParseHex(std::string_view text, uint64_t& value) {
			auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
			return ec == std::errc() && ptr == text.data() + text.size();
		}
	}

	std::vector<MountEntry> ProcFs::ParseMountInfo(std::string_view text) {
		std::vector<MountEntry> mounts;
		while (!text.empty()) {
			auto line = NextLine(text);
			NextField(line);	// mount id
			NextField(line);	// parent id
			auto device = NextField(line);
			auto root = NextField(line);
			auto mountPoint = NextField(line);
			auto colon = device.find(':');
			MountEntry mount;
			if (mountPoint.empty() || colon == std::string_view::npos || !ToNumber(device.substr(0, colon), mount.Major)
				|| !ToNumber(device.substr(colon + 1), mount.Minor))
				continue;
			// the options, then optional fields up to a lone "-"
			for (auto field = NextField(line); !field.empty() && field != "-"; field = NextField(line)) {
			}
			mount.Root = Unescape(root);
			mount.MountPoint = Unescape(mountPoint);
			mount.FileSystem = std::string(NextField(line));
			mount.Source = Unescape(NextField(line));
			mounts.push_back(std::move(mount));
		}
		return mounts;
	}

	std::vector<MemoryMap> ProcFs::ParseMaps(std::string_view text) {
		std::vector<MemoryMap> maps;
		while (!text.empty()) {
			auto line = NextLine(text);
			auto rest = line;
			auto first = NextField(rest);
			auto dash = first.find('-');
			MemoryMap map;
			if (dash != std::string_view::npos && ParseHex(first.substr(0, dash), map.Start) && ParseHex(first.substr(dash + 1), map.End)) {
				// "start-end perms offset dev inode path"
				map.Permissions = std::string(NextField(rest));
				if (!ParseHex(NextField(rest), map.Offset))
					continue;
				map.Device = std::string(NextField(rest));
				if (!ToNumber(NextField(rest), map.Inode))
					continue;
				rest.remove_prefix(std::min(rest.find_first_not_of(" \t"), rest.size()));
				map.Path = std::string(rest);
				maps.push_back(std::move(map));
				continue;
			}
			// smaps: "Rss:   120 kB" belongs to the region before it
			if (maps.empty() || first.empty() || first.back() != ':')
				continue;
			uint64_t kb;
			if (!ToNumber(NextField(rest), kb))
				continue;
			auto& last = maps.back();
			if (first == "Rss:")
				last.Resident = kb * 1024;
			else if (first == "Pss:")
				last.Proportional = kb * 1024;
			else if (first == "Swap:")
				last.Swap = kb * 1024;
		}
		return maps;
	}

	FdInfo ProcFs::ParseFdInfo(std::string_view text) {
		FdInfo info;
		while (!text.empty()) {
			auto line = NextLine(text);
			auto key = NextField(line);
			auto value = NextField(line);
			if (key == "pos:")
				ToNumber(value, info.Position);
			else if (key == "flags:") {
				auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), info.Flags, 8);
				if (ec != std::errc())
					info.Flags = 0;
			}
		}
		return info;
	}

	FileKind ProcFs::ClassifyFdTarget(std::string_view target) {
		if (target.starts_with("socket:["))
			return FileKind::Socket;
		if (target.starts_with("pipe:["))
			return FileKind::Pipe;
		if (target.starts_with("anon_inode:"))
			return FileKind::AnonymousInode;
		if (target.starts_with("/dev/"))
			return FileKind::Device;
		if (target.starts_with('/'))
			return FileKind::File;
		return FileKind::Other;
	}

	std::vector<std::pair<std::string, std::string>> ProcFs::ParseEnviron(std::string_view text) {
		std::vector<std::pair<std::string, std::string>> variables;
		while (!text.empty()) {
			auto end = text.find('\0');
			auto entry = text.substr(0, end);
			text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
			if (entry.empty())
				continue;
			auto equals = entry.find('=');
			if (equals == std::string_view::npos)
				variables.emplace_back(std::string(entry), std::string());
			else
				variables.emplace_back(std::string(entry.substr(0, equals)), std::string(entry.substr(equals + 1)));
		}
		return variables;
	}

	std::string ProcFs::ImageNameFromCommandLine(std::string_view cmdline, std::string_view comm) {
		if (comm.size() < MaxCommLength)
			return std::string(comm);
		// argv[0], or argv[1] for a script run by its interpreter (python3 /usr/bin/unattended-upgrade)
		for (int i = 0; i < 2 && !cmdline.empty(); i++) {
			auto end = cmdline.find('\0');
			auto arg = cmdline.substr(0, end);
			cmdline.remove_prefix(end == std::string_view::npos ? cmdline.size() : end + 1);
			// a process that rewrote its arguments (sshd: "sshd: user@pts/0") may have put spaces in argv[0]
			arg = arg.substr(0, arg.find(' '));
			if (auto slash = arg.rfind('/'); slash != std::string_view::npos)
				arg.remove_prefix(slash + 1);
			if (arg.size() > comm.size() && arg.starts_with(comm))
				return std::string(arg);
		}
		return std::string(comm);
	}

	std::optional<std::string> ProcFs::ReadFile(const std::string& path) {
		int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			return std::nullopt;
		std::string text;
		char buffer[4096];
		for (;;) {
			auto count = ::read(fd, buffer, sizeof(buffer));
			if (count < 0) {
				::close(fd);
				return std::nullopt;
			}
			if (count == 0)
				break;
			text.append(buffer, count);
		}
		::close(fd);
		return text;
	}

	std::optional<std::string> ProcFs::ReadLink(const std::string& path) {
		char buffer[4096];
		auto count = ::readlink(path.c_str(), buffer, sizeof(buffer));
		if (count < 0 || count == sizeof(buffer))
			return std::nullopt;
		return std::string(buffer, count);
	}

	uint64_t ProcFs::ClockTicksPerSecond() {
		static const uint64_t ticks = [] {
			auto value = ::sysconf(_SC_CLK_TCK);
			return value > 0 ? static_cast<uint64_t>(value) : 100;
		}();
		return ticks;
	}

	uint64_t ProcFs::PageSize() {
		static const uint64_t size = [] {
			auto value = ::sysconf(_SC_PAGESIZE);
			return value > 0 ? static_cast<uint64_t>(value) : 4096;
		}();
		return size;
	}

	uint32_t ProcFs::ProcessorCount() {
		static const uint32_t count = [] {
			auto value = ::sysconf(_SC_NPROCESSORS_ONLN);
			return value > 0 ? static_cast<uint32_t>(value) : 1;
		}();
		return count;
	}
}
