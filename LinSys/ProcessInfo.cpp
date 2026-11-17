#include "ProcessInfo.h"
#include "ProcFs.h"
#include "Users.h"

namespace LinSys {
	const std::string& ProcessInfo::GetCommandLine() const {
		if (!m_CommandLineRead) {
			m_CommandLineRead = true;
			if (auto text = ProcFs::ReadFile(m_ProcRoot + "/" + std::to_string(Id) + "/cmdline"))
				m_CommandLine = ProcFs::ParseCommandLine(*text);
		}
		return m_CommandLine;
	}

	const std::string& ProcessInfo::GetUserName() const {
		if (m_UserName.empty())
			m_UserName = Users::GetUserName(Uid);
		return m_UserName;
	}
}
