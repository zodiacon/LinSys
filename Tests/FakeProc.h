#pragma once

//
// A folder laid out as /proc, removed with the object: a ProcessManager given its Root reads processes the test made,
// so new, terminated and reused ids, CPU and the tree are checked deterministically. LinSysLive's tests use it too.
//

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace Tests {
	class FakeProc {
	public:
		FakeProc() {
			char path[] = "/tmp/linsys-test-XXXXXX";
			m_Root = ::mkdtemp(path);
		}
		~FakeProc() {
			std::error_code ec;
			std::filesystem::remove_all(m_Root, ec);
		}
		FakeProc(const FakeProc&) = delete;
		FakeProc& operator=(const FakeProc&) = delete;

		[[nodiscard]] const std::string& Root() const { return m_Root; }

		// times in clock ticks
		void SetProcess(uint32_t pid, const std::string& comm, uint32_t ppid, uint64_t utime, uint64_t stime, uint64_t start,
			uint32_t uid = 1000) {
			auto dir = m_Root + "/" + std::to_string(pid);
			std::filesystem::create_directories(dir + "/task");
			Write(dir + "/stat", StatLine(pid, comm, ppid, utime, stime, start));
			Write(dir + "/status", "Name:\t" + comm + "\nUid:\t" + std::to_string(uid) + "\t0\t0\t0\n");
		}
		void SetThread(uint32_t pid, uint32_t tid, const std::string& comm, uint64_t utime, uint64_t stime, uint64_t start) {
			auto dir = m_Root + "/" + std::to_string(pid) + "/task/" + std::to_string(tid);
			std::filesystem::create_directories(dir);
			Write(dir + "/stat", StatLine(tid, comm, 1, utime, stime, start));
		}
		void Remove(uint32_t pid) {
			std::filesystem::remove_all(m_Root + "/" + std::to_string(pid));
		}

	private:
		static void Write(const std::string& path, const std::string& text) {
			std::ofstream(path) << text;
		}
		static std::string StatLine(uint32_t pid, const std::string& comm, uint32_t ppid, uint64_t utime, uint64_t stime, uint64_t start) {
			auto n = [](auto value) { return std::to_string(value) + " "; };
			// fields 3 to 24 as proc(5) numbers them, then zeros to 52
			std::string line = n(pid) + "(" + comm + ") S " + n(ppid) + "0 0 0 -1 0 0 0 0 0 " + n(utime) + n(stime)
				+ "0 0 20 0 1 0 " + n(start) + "4096 10";
			for (int i = 25; i <= 52; i++)
				line += " 0";
			return line + "\n";
		}

		std::string m_Root;
	};
}
