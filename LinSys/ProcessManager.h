#pragma once

#include <cstdint>
#include <dirent.h>
#include <memory>
#include <string>
#include <string_view>
#include <time.h>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "Keys.h"
#include "ProcFs.h"
#include "ProcessInfo.h"
#include "ThreadInfo.h"

namespace LinSys {
	// Snapshots of the processes (and, when asked, their threads) from /proc, each diffed against the one before by
	// ProcessOrThreadKey: the new and the terminated ones, and the CPU each took between them. The same shape as
	// WinSys::ProcessManager, so the app's models translate; callers can derive from ProcessInfo/ThreadInfo to attach
	// data of their own.
	//
	// Differences from WinSys's: the objects hold copies of their values (no snapshot buffer to keep alive), and /proc
	// is read file by file, so a process can exit midway; it's then left out, and reported as terminated next time.
	template<typename TProcessInfo = ProcessInfo, typename TThreadInfo = ThreadInfo>
	class ProcessManager {
		static_assert(std::is_base_of_v<ProcessInfo, TProcessInfo>);
		static_assert(std::is_base_of_v<ThreadInfo, TThreadInfo>);
	public:
		// procRoot: a folder laid out as /proc (tests make one); processors: what CPU is divided by (0: the system's)
		explicit ProcessManager(std::string procRoot = "/proc", uint32_t processors = 0)
			: m_ProcRoot(std::move(procRoot)), m_Processors(processors ? processors : ProcFs::ProcessorCount()) {
		}
		ProcessManager(const ProcessManager&) = delete;
		ProcessManager& operator=(const ProcessManager&) = delete;

		uint32_t Update() {
			return Update(false, 0);
		}
		// pid: only that process's threads (every process is updated). Asking for another process's threads starts
		// over: the previous one's aren't reported as terminated, and the new one's are all new
		uint32_t UpdateWithThreads(uint32_t pid = 0) {
			return Update(true, pid);
		}
		uint32_t Update(bool includeThreads, uint32_t pid) {
			return UpdateAt(includeThreads, pid, MonotonicNanoseconds());
		}

		// Update, at a time given in nanoseconds (on any clock that only goes forward); tests pass their own
		uint32_t UpdateAt(bool includeThreads, uint32_t pid, int64_t now) {
			auto dir = ::opendir(m_ProcRoot.c_str());
			if (!dir)	// the previous snapshot stays
				return static_cast<uint32_t>(m_Processes.size());

			int64_t elapsed = m_PrevTime == 0 ? 0 : now - m_PrevTime;

			std::vector<std::shared_ptr<TProcessInfo>> processes;
			processes.reserve(m_Processes.empty() ? 512 : m_Processes.size() + 16);
			ProcessMap processesByKey;
			processesByKey.reserve(processes.capacity());
			m_ProcessesById.clear();
			m_NewProcesses.clear();

			ThreadMap threadsByKey;
			if (includeThreads && pid != m_ThreadsPid) {
				m_ThreadsByKey.clear();
				m_ThreadsPid = pid;
			}
			if (includeThreads) {
				threadsByKey.reserve(m_Threads.empty() ? 4096 : m_Threads.size() + 64);
				m_NewThreads.clear();
				m_Threads.clear();
			}

			while (auto entry = ::readdir(dir)) {
				uint32_t id;
				if (!ParseId(entry->d_name, id))
					continue;
				auto path = m_ProcRoot + "/" + entry->d_name;
				auto text = ProcFs::ReadFile(path + "/stat");
				if (!text)
					continue;	// it exited
				auto stat = ProcFs::ParseStat(*text);
				if (!stat || stat->Pid != id)
					continue;

				ProcessOrThreadKey key{ static_cast<int64_t>(stat->StartTime), id };
				std::shared_ptr<TProcessInfo> pi;
				if (auto it = m_ProcessesByKey.find(key); it == m_ProcessesByKey.end()) {
					pi = std::make_shared<TProcessInfo>();
					InitProcess(*pi, *stat, key, path);
					FillProcess(*pi, *stat);
					m_NewProcesses.push_back(pi);
				}
				else {
					pi = it->second;
					auto total = pi->UserTime + pi->KernelTime;
					auto kernel = pi->KernelTime;
					FillProcess(*pi, *stat);
					pi->CPU = CpuOf(pi->UserTime + pi->KernelTime - total, elapsed);
					pi->KernelCPU = CpuOf(pi->KernelTime - kernel, elapsed);
					m_ProcessesByKey.erase(it);
				}

				if (includeThreads && (pid == 0 || pid == id))
					UpdateThreads(*pi, path, threadsByKey, elapsed);

				processes.push_back(pi);
				processesByKey.emplace(key, pi);
				m_ProcessesById.emplace(id, pi);
			}
			::closedir(dir);

			m_Processes = std::move(processes);

			// what's left wasn't seen: terminated
			m_TerminatedProcesses.clear();
			m_TerminatedProcesses.reserve(m_ProcessesByKey.size());
			for (auto& [key, pi] : m_ProcessesByKey)
				m_TerminatedProcesses.push_back(pi);
			m_ProcessesByKey = std::move(processesByKey);

			if (includeThreads) {
				m_TerminatedThreads.clear();
				m_TerminatedThreads.reserve(m_ThreadsByKey.size());
				for (auto& [key, ti] : m_ThreadsByKey)
					m_TerminatedThreads.push_back(ti);
				m_ThreadsByKey = std::move(threadsByKey);
			}

			m_PrevTime = now;
			return static_cast<uint32_t>(m_Processes.size());
		}

		[[nodiscard]] const std::vector<std::shared_ptr<TProcessInfo>>& GetProcesses() const { return m_Processes; }
		[[nodiscard]] const std::vector<std::shared_ptr<TProcessInfo>>& GetNewProcesses() const { return m_NewProcesses; }
		[[nodiscard]] const std::vector<std::shared_ptr<TProcessInfo>>& GetTerminatedProcesses() const { return m_TerminatedProcesses; }
		[[nodiscard]] size_t GetProcessCount() const { return m_Processes.size(); }

		[[nodiscard]] std::shared_ptr<TProcessInfo> GetProcessById(uint32_t pid) const {
			auto it = m_ProcessesById.find(pid);
			return it == m_ProcessesById.end() ? nullptr : it->second;
		}
		[[nodiscard]] std::shared_ptr<TProcessInfo> GetProcessByKey(const ProcessOrThreadKey& key) const {
			auto it = m_ProcessesByKey.find(key);
			return it == m_ProcessesByKey.end() ? nullptr : it->second;
		}
		[[nodiscard]] std::string GetProcessNameById(uint32_t pid) const {
			auto pi = GetProcessById(pid);
			return pi ? pi->GetImageName() : std::string();
		}

		[[nodiscard]] const std::vector<std::shared_ptr<TThreadInfo>>& GetThreads() const { return m_Threads; }
		[[nodiscard]] const std::vector<std::shared_ptr<TThreadInfo>>& GetNewThreads() const { return m_NewThreads; }
		[[nodiscard]] const std::vector<std::shared_ptr<TThreadInfo>>& GetTerminatedThreads() const { return m_TerminatedThreads; }
		[[nodiscard]] size_t GetThreadCount() const { return m_Threads.size(); }
		[[nodiscard]] std::shared_ptr<TThreadInfo> GetThreadByKey(const ProcessOrThreadKey& key) const {
			auto it = m_ThreadsByKey.find(key);
			return it == m_ThreadsByKey.end() ? nullptr : it->second;
		}

		// The last snapshot as a tree, depth first: each process with its depth. A process is a root when its parent
		// isn't in the snapshot or started after it (the parent id was reused); children come in snapshot order
		[[nodiscard]] std::vector<std::pair<std::shared_ptr<TProcessInfo>, int>> BuildProcessTree() const {
			std::unordered_map<uint32_t, std::vector<TProcessInfo*>> children;
			std::vector<TProcessInfo*> roots;
			for (auto& p : m_Processes) {
				auto parent = GetProcessById(p->ParentId);
				// start times are in clock ticks, so a child can share its parent's
				if (p->ParentId == 0 || p->ParentId == p->Id || !parent || parent->Key.Created > p->Key.Created)
					roots.push_back(p.get());
				else
					children[p->ParentId].push_back(p.get());
			}

			std::vector<std::pair<std::shared_ptr<TProcessInfo>, int>> tree;
			tree.reserve(m_Processes.size());
			std::unordered_set<uint32_t> visited;
			auto add = [&](auto&& self, TProcessInfo* p, int depth) -> void {
				if (!visited.insert(p->Id).second)
					return;
				tree.emplace_back(GetProcessById(p->Id), depth);
				if (auto it = children.find(p->Id); it != children.end())
					for (auto child : it->second)
						self(self, child, depth + 1);
			};
			for (auto root : roots)
				add(add, root, 0);
			return tree;
		}

	private:
		using ProcessMap = std::unordered_map<ProcessOrThreadKey, std::shared_ptr<TProcessInfo>>;
		using ThreadMap = std::unordered_map<ProcessOrThreadKey, std::shared_ptr<TThreadInfo>>;

		static int64_t MonotonicNanoseconds() {
			timespec ts;
			::clock_gettime(CLOCK_MONOTONIC, &ts);
			return static_cast<int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
		}

		static bool ParseId(const char* name, uint32_t& id) {
			if (*name < '1' || *name > '9')
				return false;
			uint64_t value = 0;
			for (; *name; name++) {
				if (*name < '0' || *name > '9')
					return false;
				value = value * 10 + (*name - '0');
				if (value > UINT32_MAX)
					return false;
			}
			id = static_cast<uint32_t>(value);
			return true;
		}

		static uint64_t TicksToNanoseconds(uint64_t ticks) {
			auto hz = ProcFs::ClockTicksPerSecond();
			return ticks / hz * 1000000000 + ticks % hz * 1000000000 / hz;
		}

		// time in nanoseconds over the elapsed time, in 1/10000 of a percent of all processors
		int32_t CpuOf(uint64_t time, int64_t elapsed) const {
			if (elapsed <= 0 || static_cast<int64_t>(time) < 0)
				return 0;
			return static_cast<int32_t>(static_cast<int64_t>(time) * 1000000 / elapsed / m_Processors);
		}

		template<typename T>
		static void FillCommon(T& info, const StatFields& stat) {
			info.State = stat.State;
			info.Priority = stat.Priority;
			info.Nice = stat.Nice;
			info.Processor = stat.Processor;
			info.UserTime = TicksToNanoseconds(stat.UserTime);
			info.KernelTime = TicksToNanoseconds(stat.KernelTime);
		}

		static void FillProcess(TProcessInfo& pi, const StatFields& stat) {
			FillCommon(pi, stat);
			pi.ParentId = stat.ParentPid;	// it changes when the parent exits (reparenting)
			pi.Flags = stat.Flags;
			pi.ThreadCount = stat.ThreadCount;
			pi.VirtualSize = stat.VirtualSize;
			pi.ResidentSize = stat.ResidentPages * ProcFs::PageSize();
		}

		void InitProcess(TProcessInfo& pi, const StatFields& stat, const ProcessOrThreadKey& key, const std::string& path) const {
			pi.Id = stat.Pid;
			pi.Key = key;
			pi.StartTime = TicksToNanoseconds(stat.StartTime);
			pi.m_ProcRoot = m_ProcRoot;
			pi.m_Comm = stat.Comm;
			if (auto status = ProcFs::ReadFile(path + "/status"))
				pi.Uid = ProcFs::ParseStatusUid(*status).value_or(0);
			if (auto exe = ProcFs::ReadLink(path + "/exe")) {
				std::string_view image = *exe;
				if (image.ends_with(" (deleted)"))
					image.remove_suffix(10);
				pi.m_ImagePath = image;
			}
			if (pi.m_ImagePath.empty()) {
				// kernel threads have no command line
				auto cmdline = (stat.Flags & ProcFs::KernelThreadFlag) ? std::nullopt : ProcFs::ReadFile(path + "/cmdline");
				pi.m_ImageName = cmdline ? ProcFs::ImageNameFromCommandLine(*cmdline, stat.Comm) : stat.Comm;
			}
			else {
				auto slash = pi.m_ImagePath.rfind('/');
				pi.m_ImageName = slash == std::string::npos ? pi.m_ImagePath : pi.m_ImagePath.substr(slash + 1);
			}
		}

		void UpdateThreads(TProcessInfo& pi, const std::string& path, ThreadMap& threadsByKey, int64_t elapsed) {
			pi.m_Threads.clear();
			auto taskPath = path + "/task";
			auto dir = ::opendir(taskPath.c_str());
			if (!dir)
				return;
			while (auto entry = ::readdir(dir)) {
				uint32_t tid;
				if (!ParseId(entry->d_name, tid))
					continue;
				auto text = ProcFs::ReadFile(taskPath + "/" + entry->d_name + "/stat");
				if (!text)
					continue;
				auto stat = ProcFs::ParseStat(*text);
				if (!stat || stat->Pid != tid)
					continue;

				ProcessOrThreadKey key{ static_cast<int64_t>(stat->StartTime), tid };
				std::shared_ptr<TThreadInfo> ti;
				if (auto it = m_ThreadsByKey.find(key); it == m_ThreadsByKey.end()) {
					ti = std::make_shared<TThreadInfo>();
					ti->Id = tid;
					ti->ProcessId = pi.Id;
					ti->Key = key;
					ti->m_ProcessName = pi.GetImageName();
					FillCommon(*ti, *stat);
					m_NewThreads.push_back(ti);
				}
				else {
					ti = it->second;
					auto total = ti->UserTime + ti->KernelTime;
					FillCommon(*ti, *stat);
					ti->CPU = CpuOf(ti->UserTime + ti->KernelTime - total, elapsed);
					m_ThreadsByKey.erase(it);
				}
				ti->m_Name = stat->Comm;	// a thread can rename itself
				pi.m_Threads.push_back(ti);
				m_Threads.push_back(ti);
				threadsByKey.emplace(key, ti);
			}
			::closedir(dir);
		}

		std::string m_ProcRoot;
		uint32_t m_Processors;
		int64_t m_PrevTime{ 0 };

		std::vector<std::shared_ptr<TProcessInfo>> m_Processes;
		std::vector<std::shared_ptr<TProcessInfo>> m_NewProcesses;
		std::vector<std::shared_ptr<TProcessInfo>> m_TerminatedProcesses;
		std::unordered_map<uint32_t, std::shared_ptr<TProcessInfo>> m_ProcessesById;
		ProcessMap m_ProcessesByKey;

		std::vector<std::shared_ptr<TThreadInfo>> m_Threads;
		std::vector<std::shared_ptr<TThreadInfo>> m_NewThreads;
		std::vector<std::shared_ptr<TThreadInfo>> m_TerminatedThreads;
		ThreadMap m_ThreadsByKey;
		uint32_t m_ThreadsPid{ 0 };	// the process whose threads were taken last (0: all)
	};
}
