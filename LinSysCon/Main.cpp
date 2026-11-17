// LinSysCon: LinSys at work, as WinSysCon is WinSys's. Samples for a second, then prints the processes by CPU
// (or, with --tree, as a tree) and the system's CPU and memory; or, with --services, systemd's services and how long
// listing them took.
//
// linsyscon [--tree] [-n count] | --services

#include <LinSys/ProcessManager.h>
#include <LinSys/Services.h>
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

namespace {
	int ListServices() {
		using Clock = std::chrono::steady_clock;
		auto ms = [](Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
		ServiceManager manager;
		std::vector<ServiceInfo> services;
		auto start = Clock::now();
		auto result = manager.List(services, true);
		auto first = Clock::now() - start;
		start = Clock::now();
		result = manager.List(services, false);
		auto second = Clock::now() - start;
		if (result.Error) {
			fprintf(stderr, "%s (%d)\n", result.Message.c_str(), result.Error);
			return 1;
		}
		for (auto& s : services)
			printf("%-48s %-10s %-10s %-9s %7u  %s\n", s.Name.c_str(), s.ActiveState.c_str(), s.SubState.c_str(),
				s.UnitFileState.c_str(), s.MainPid, s.Description.c_str());
		printf("\n%zu services; listed in %.1f ms with the unit files, %.1f ms without\n", services.size(), ms(first), ms(second));
		return 0;
	}
}

int main(int argc, char** argv) {
	bool tree = false;
	size_t count = 25;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--services") == 0)
			return ListServices();
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
