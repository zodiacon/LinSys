#include "Process.h"
#include "ProcFs.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <sys/syscall.h>
#include <unistd.h>

// pidfd_open and pidfd_send_signal by their system calls: glibc 2.43's <sys/pidfd.h> declares them without C linkage
// (they don't link from C++), and older ones lack them

namespace LinSys {
	int Process::SendSignal(const ProcessOrThreadKey& key, int signal, const std::string& procRoot) {
		int fd = static_cast<int>(::syscall(SYS_pidfd_open, static_cast<pid_t>(key.Id), 0));
		if (fd < 0)
			return errno;
		// the id could have been reused before the pidfd was opened; once open, the pidfd is that process's,
		// so a start time read now is the one to compare
		int result = 0;
		auto text = ProcFs::ReadFile(procRoot + "/" + std::to_string(key.Id) + "/stat");
		auto stat = text ? ProcFs::ParseStat(*text) : std::nullopt;
		if (!stat || static_cast<int64_t>(stat->StartTime) != key.Created)
			result = ESRCH;
		else if (::syscall(SYS_pidfd_send_signal, fd, signal, nullptr, 0) < 0)
			result = errno;
		::close(fd);
		return result;
	}

	int Process::Terminate(const ProcessOrThreadKey& key) {
		return SendSignal(key, SIGTERM);
	}

	int Process::Kill(const ProcessOrThreadKey& key) {
		return SendSignal(key, SIGKILL);
	}

	int Process::Suspend(const ProcessOrThreadKey& key) {
		return SendSignal(key, SIGSTOP);
	}

	int Process::Resume(const ProcessOrThreadKey& key) {
		return SendSignal(key, SIGCONT);
	}

	std::string Process::ErrorText(int error) {
		char buffer[256];
		// the GNU strerror_r: returns the text, which may not be in the buffer
		return ::strerror_r(error, buffer, sizeof(buffer));
	}
}
