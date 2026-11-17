// LinSysCon: LinSys at work, as WinSysCon is WinSys's. Samples for a second, then prints the processes by CPU
// (or, with --tree, as a tree) and the system's CPU and memory.
//
// linsyscon [--tree] [-n count]

#include <LinSys/ProcessManager.h>
#include <LinSys/SystemStats.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

using namespace LinSys;

namespace {
	std::string Percent(int32_t cpu) {
		char text[16];
		snprintf(text, sizeof(text), "%6.2f", cpu / 10000.0);
		return text;
	}

	void PrintProcess(const ProcessInfo& p, int depth) {
		printf("%7u %7u %c %s %8llu MB  %-10s %*s%s\n", p.Id, p.ParentId, p.State, Percent(p.CPU).c_str(),
			static_cast<unsigned long long>(p.ResidentSize >> 20), p.GetUserName().c_str(), depth * 2, "",
			p.GetImageName().c_str());
	}
}

int main(int argc, char** argv) {
	bool tree = false;
	size_t count = 25;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--tree") == 0)
			tree = true;
		else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
			count = std::stoul(argv[++i]);
		else {
			fprintf(stderr, "usage: %s [--tree] [-n count]\n", argv[0]);
			return 1;
		}
	}

	ProcessManager<> pm;
	SystemStats stats;
	pm.Update();
	stats.Update();
	std::this_thread::sleep_for(std::chrono::seconds(1));
	pm.Update();
	stats.Update();

	auto& memory = stats.GetMemory();
	printf("CPU %s%% (kernel %s%%), %u processors; memory %llu of %llu MB available; %zu processes\n\n",
		Percent(stats.GetCpu()).c_str(), Percent(stats.GetKernelCpu()).c_str(), ProcFs::ProcessorCount(),
		static_cast<unsigned long long>(memory.Available >> 20), static_cast<unsigned long long>(memory.Total >> 20),
		pm.GetProcessCount());
	printf("%7s %7s S %6s %11s  %-10s %s\n", "PID", "PPID", "CPU%", "Resident", "User", "Name");

	if (tree) {
		for (auto& [p, depth] : pm.BuildProcessTree())
			PrintProcess(*p, depth);
		return 0;
	}

	auto processes = pm.GetProcesses();
	std::sort(processes.begin(), processes.end(), [](auto& a, auto& b) {
		return a->CPU != b->CPU ? a->CPU > b->CPU : a->ResidentSize > b->ResidentSize;
	});
	processes.resize(std::min(count, processes.size()));
	for (auto& p : processes)
		PrintProcess(*p, 0);
	return 0;
}
