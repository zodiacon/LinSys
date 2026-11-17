#include "TestFramework.h"
#include <LinSys/Process.h>
#include <LinSys/ProcFs.h>

#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

using namespace LinSys;

namespace {
	// a child that waits to be signalled, and its key
	pid_t StartChild(ProcessOrThreadKey& key) {
		pid_t pid = ::fork();
		if (pid == 0) {
			for (;;)
				::pause();
		}
		auto text = ProcFs::ReadFile("/proc/" + std::to_string(pid) + "/stat");
		auto stat = text ? ProcFs::ParseStat(*text) : std::nullopt;
		key = { stat ? static_cast<int64_t>(stat->StartTime) : 0, static_cast<uint32_t>(pid) };
		return pid;
	}

	bool IsRunning(pid_t pid) {
		int status;
		return ::waitpid(pid, &status, WNOHANG) == 0;
	}
}

TEST(Process_Terminate) {
	ProcessOrThreadKey key;
	auto pid = StartChild(key);
	CHECK(key.Created != 0);
	CHECK_EQ(Process::Terminate(key), 0);
	int status = 0;
	CHECK_EQ(::waitpid(pid, &status, 0), pid);
	CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
}

// the same id with another start time is another process: left alone
TEST(Process_WrongStartTime) {
	ProcessOrThreadKey key;
	auto pid = StartChild(key);
	CHECK_EQ(Process::Kill({ key.Created + 1, key.Id }), ESRCH);
	CHECK(IsRunning(pid));

	CHECK_EQ(Process::Suspend(key), 0);
	CHECK_EQ(Process::Resume(key), 0);
	CHECK_EQ(Process::Kill(key), 0);
	int status = 0;
	::waitpid(pid, &status, 0);
	CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
}

TEST(Process_Gone) {
	ProcessOrThreadKey key;
	auto pid = StartChild(key);
	CHECK_EQ(Process::Kill(key), 0);
	int status;
	::waitpid(pid, &status, 0);
	CHECK_EQ(Process::Kill(key), ESRCH);	// reaped: no such process
	CHECK(!Process::ErrorText(ESRCH).empty());
}
