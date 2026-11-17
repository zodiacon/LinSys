#include "SystemStats.h"

namespace LinSys {
	namespace {
		int32_t Share(uint64_t part, uint64_t total) {
			return total == 0 ? 0 : static_cast<int32_t>(part * 1000000 / total);
		}
	}

	bool SystemStats::Update() {
		auto stat = ProcFs::ReadFile(m_ProcRoot + "/stat");
		auto meminfo = ProcFs::ReadFile(m_ProcRoot + "/meminfo");
		return stat && meminfo && UpdateFrom(*stat, *meminfo);
	}

	bool SystemStats::UpdateFrom(std::string_view stat, std::string_view meminfo) {
		auto cpus = ProcFs::ParseCpuTimes(stat);
		auto memory = ProcFs::ParseMemInfo(meminfo);
		if (cpus.empty() || !memory)
			return false;

		m_Memory = *memory;
		// a processor coming online or going offline changes the lines: start over
		if (m_Prev.size() == cpus.size()) {
			auto& now = cpus[0];
			auto& prev = m_Prev[0];
			auto total = now.Total() - prev.Total();
			m_Cpu = Share(now.Busy() - prev.Busy(), total);
			m_KernelCpu = Share(now.System + now.Irq + now.SoftIrq - prev.System - prev.Irq - prev.SoftIrq, total);
			m_ProcessorCpu.resize(cpus.size() - 1);
			for (size_t i = 1; i < cpus.size(); i++)
				m_ProcessorCpu[i - 1] = Share(cpus[i].Busy() - m_Prev[i].Busy(), cpus[i].Total() - m_Prev[i].Total());
		}
		else {
			m_Cpu = m_KernelCpu = 0;
			m_ProcessorCpu.assign(cpus.size() - 1, 0);
		}
		m_Prev = std::move(cpus);
		return true;
	}
}
