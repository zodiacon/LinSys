#pragma once

#include <cstdint>
#include <string>
#include "Keys.h"

namespace LinSys {
	// Operations on a process, named by its key (id and start time) so a reused id is never acted on: the process is
	// opened as a pidfd, its start time checked, then the pidfd is used. Each returns 0 or an errno value (ESRCH: it's
	// gone, or the id is another process's now; EPERM: not allowed)
	struct Process final {
		[[nodiscard]] static int SendSignal(const ProcessOrThreadKey& key, int signal, const std::string& procRoot = "/proc");
		[[nodiscard]] static int Terminate(const ProcessOrThreadKey& key);	// SIGTERM: asks it to exit
		[[nodiscard]] static int Kill(const ProcessOrThreadKey& key);		// SIGKILL
		[[nodiscard]] static int Suspend(const ProcessOrThreadKey& key);	// SIGSTOP
		[[nodiscard]] static int Resume(const ProcessOrThreadKey& key);		// SIGCONT
		// the error's text (strerror)
		[[nodiscard]] static std::string ErrorText(int error);
	};
}
