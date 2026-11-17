#include "TestFramework.h"
#include "FakeProc.h"
#include <LinSys/ProcessManager.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <unistd.h>

using namespace LinSys;
namespace fs = std::filesystem;

namespace {
	using Tests::FakeProc;

	constexpr int64_t Second = 1000000000;

	template<typename T>
	bool Contains(const std::vector<std::shared_ptr<T>>& items, uint32_t id) {
		return std::any_of(items.begin(), items.end(), [=](auto& p) { return p->Id == id; });
	}
}

TEST(ProcessManager_NewAndTerminated) {
	FakeProc proc;
	proc.SetProcess(1, "init", 0, 0, 0, 1);
	proc.SetProcess(100, "bash", 1, 0, 0, 50);
	ProcessManager<> pm(proc.Root(), 1);

	CHECK_EQ(pm.UpdateAt(false, 0, Second), 2u);
	CHECK_EQ(pm.GetNewProcesses().size(), 2u);
	CHECK(pm.GetTerminatedProcesses().empty());
	auto bash = pm.GetProcessById(100);
	CHECK(bash != nullptr);
	if (bash) {
		CHECK_EQ(bash->GetImageName(), std::string("bash"));	// no exe link: comm
		CHECK_EQ(bash->ParentId, 1u);
		CHECK_EQ(bash->Uid, 1000u);
		CHECK_EQ(bash->State, 'S');
	}

	proc.Remove(100);
	proc.SetProcess(200, "vim", 1, 0, 0, 60);
	pm.UpdateAt(false, 0, 2 * Second);
	CHECK_EQ(pm.GetNewProcesses().size(), 1u);
	CHECK(Contains(pm.GetNewProcesses(), 200));
	CHECK_EQ(pm.GetTerminatedProcesses().size(), 1u);
	CHECK(Contains(pm.GetTerminatedProcesses(), 100));
	CHECK(pm.GetProcessById(100) == nullptr);
	// the same object across updates
	CHECK(pm.GetProcessById(1) == pm.GetProcessByKey({ 1, 1 }));
}

// a reused id with another start time is another process
TEST(ProcessManager_ReusedId) {
	FakeProc proc;
	proc.SetProcess(100, "old", 0, 0, 0, 10);
	ProcessManager<> pm(proc.Root(), 1);
	pm.UpdateAt(false, 0, Second);
	auto old = pm.GetProcessById(100);

	proc.SetProcess(100, "new", 0, 0, 0, 20);
	pm.UpdateAt(false, 0, 2 * Second);
	CHECK(Contains(pm.GetNewProcesses(), 100));
	CHECK(Contains(pm.GetTerminatedProcesses(), 100));
	CHECK(pm.GetProcessById(100) != old);
	CHECK_EQ(pm.GetProcessById(100)->GetComm(), std::string("new"));
}

TEST(ProcessManager_Cpu) {
	FakeProc proc;
	auto hz = ProcFs::ClockTicksPerSecond();
	proc.SetProcess(100, "busy", 0, 0, 0, 10);
	ProcessManager<> pm(proc.Root(), 4);
	pm.UpdateAt(false, 0, Second);
	CHECK_EQ(pm.GetProcessById(100)->CPU, 0);

	// over 2 seconds on 4 processors: 1 second of user time and half a second of kernel time is 18.75%
	proc.SetProcess(100, "busy", 0, hz, hz / 2, 10);
	pm.UpdateAt(false, 0, 3 * Second);
	auto busy = pm.GetProcessById(100);
	CHECK_EQ(busy->CPU, 187500);
	CHECK_EQ(busy->KernelCPU, 62500);
	CHECK_EQ(busy->UserTime, static_cast<uint64_t>(Second));
	CHECK_EQ(busy->KernelTime, static_cast<uint64_t>(Second / 2));
}

TEST(ProcessManager_Threads) {
	FakeProc proc;
	auto hz = ProcFs::ClockTicksPerSecond();
	proc.SetProcess(100, "app", 0, 0, 0, 10);
	proc.SetThread(100, 100, "app", 0, 0, 10);
	proc.SetThread(100, 101, "worker", 0, 0, 11);
	proc.SetProcess(200, "other", 0, 0, 0, 10);
	proc.SetThread(200, 200, "other", 0, 0, 10);
	ProcessManager<> pm(proc.Root(), 1);

	pm.UpdateAt(true, 100, Second);
	CHECK_EQ(pm.GetThreadCount(), 2u);	// only process 100's
	CHECK_EQ(pm.GetNewThreads().size(), 2u);
	CHECK_EQ(pm.GetProcessById(100)->GetThreads().size(), 2u);

	fs::remove_all(proc.Root() + "/100/task/101");
	proc.SetThread(100, 100, "app", hz / 4, 0, 10);
	proc.SetThread(100, 102, "renamed", 0, 0, 12);
	pm.UpdateAt(true, 100, 2 * Second);
	CHECK_EQ(pm.GetThreadCount(), 2u);
	CHECK(Contains(pm.GetNewThreads(), 102));
	CHECK(Contains(pm.GetTerminatedThreads(), 101));
	auto main = pm.GetThreadByKey({ 10, 100 });
	CHECK(main != nullptr);
	if (main) {
		CHECK_EQ(main->CPU, 250000);
		CHECK_EQ(main->GetProcessImageName(), std::string("app"));
	}

	// another process's threads start over: none terminated, all new
	pm.UpdateAt(true, 200, 3 * Second);
	CHECK_EQ(pm.GetThreadCount(), 1u);
	CHECK_EQ(pm.GetNewThreads().size(), 1u);
	CHECK(pm.GetTerminatedThreads().empty());
}

TEST(ProcessManager_Tree) {
	FakeProc proc;
	proc.SetProcess(1, "init", 0, 0, 0, 1);
	proc.SetProcess(10, "sshd", 1, 0, 0, 5);
	proc.SetProcess(20, "bash", 10, 0, 0, 8);
	proc.SetProcess(30, "vim", 20, 0, 0, 9);
	proc.SetProcess(40, "cron", 1, 0, 0, 5);
	// its parent id was reused by a process started after it: a root
	proc.SetProcess(50, "orphan", 60, 0, 0, 3);
	proc.SetProcess(60, "late", 1, 0, 0, 7);
	ProcessManager<> pm(proc.Root(), 1);
	pm.UpdateAt(false, 0, Second);

	auto tree = pm.BuildProcessTree();
	CHECK_EQ(tree.size(), 7u);
	auto depth = [&](uint32_t id) {
		for (auto& [p, d] : tree)
			if (p->Id == id)
				return d;
		return -1;
	};
	CHECK_EQ(depth(1), 0);
	CHECK_EQ(depth(10), 1);
	CHECK_EQ(depth(20), 2);
	CHECK_EQ(depth(30), 3);
	CHECK_EQ(depth(40), 1);
	CHECK_EQ(depth(60), 1);
	CHECK_EQ(depth(50), 0);
	// depth first: each child right after its parent's subtree begins
	for (size_t i = 0; i < tree.size(); i++)
		if (tree[i].first->Id == 30)
			CHECK(i > 0 && tree[i - 1].first->Id == 20);
}

// this process: its name, its threads and the CPU it takes
TEST(ProcessManager_Live) {
	ProcessManager<> pm;
	CHECK(pm.Update() > 1);
	auto self = pm.GetProcessById(static_cast<uint32_t>(::getpid()));
	CHECK(self != nullptr);
	if (!self)
		return;
	CHECK(!self->GetImagePath().empty());
	CHECK_EQ(self->GetImageName(), fs::path(self->GetImagePath()).filename().string());
	CHECK_EQ(self->Uid, static_cast<uint32_t>(::getuid()));
	CHECK(!self->GetUserName().empty());
	CHECK(self->GetCommandLine().find(self->GetImageName()) != std::string::npos);
	CHECK(self->ResidentSize > 0);

	std::atomic<bool> stop{ false };
	std::thread worker([&] {
		while (!stop) {
		}
	});
	pm.UpdateWithThreads(self->Id);
	auto start = std::chrono::steady_clock::now();
	while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(300)) {
	}
	pm.UpdateWithThreads(self->Id);
	stop = true;
	worker.join();

	CHECK(pm.GetThreadCount() >= 2);
	CHECK(self->CPU > 0);
	CHECK(self->CPU <= 1000000);
}
