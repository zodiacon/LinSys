#include "SystemStats.h"

#include <algorithm>
#include <time.h>

namespace LinSys {
	namespace {
		int32_t Share(uint64_t part, uint64_t total) {
			return total == 0 ? 0 : static_cast<int32_t>(part * 1000000 / total);
		}

		// a counter's change per second; a counter that went back (the device was reset) gives 0
		uint64_t PerSecond(uint64_t now, uint64_t before, int64_t elapsed) {
			if (elapsed <= 0 || now < before)
				return 0;
			// in long double: a byte count times 10^9 can pass 64 bits (10 GB over an interval)
			return static_cast<uint64_t>(static_cast<long double>(now - before) * 1e9L / elapsed);
		}

		// disks come and go rarely (a USB drive): /sys/block is listed every this many updates
		constexpr int DiskListUpdates = 10;
	}

	bool SystemStats::Update() {
		auto stat = ProcFs::ReadFile(m_ProcRoot + "/stat");
		auto meminfo = ProcFs::ReadFile(m_ProcRoot + "/meminfo");
		if (!stat || !meminfo)
			return false;
		auto diskstats = ProcFs::ReadFile(m_ProcRoot + "/diskstats").value_or(std::string());
		auto netdev = ProcFs::ReadFile(m_ProcRoot + "/net/dev").value_or(std::string());
		if (m_UpdatesSinceList-- <= 0) {
			m_DiskList = Disks::List(m_SysRoot);
			m_UpdatesSinceList = DiskListUpdates;
		}
		timespec ts;
		::clock_gettime(CLOCK_MONOTONIC, &ts);
		return UpdateFrom({ *stat, *meminfo, diskstats, netdev }, int64_t(ts.tv_sec) * 1000000000 + ts.tv_nsec, m_DiskList);
	}

	bool SystemStats::UpdateFrom(const Sources& sources, int64_t now, const std::vector<DiskInfo>& disks) {
		auto cpus = ProcFs::ParseCpuTimes(sources.Stat);
		auto memory = ProcFs::ParseMemInfo(sources.MemInfo);
		if (cpus.empty() || !memory)
			return false;
		int64_t elapsed = m_PrevTime == 0 ? 0 : now - m_PrevTime;
		m_PrevTime = now;

		m_Memory = *memory;
		// a processor coming online or going offline changes the lines: start over
		if (m_Prev.size() == cpus.size()) {
			auto& current = cpus[0];
			auto& prev = m_Prev[0];
			auto total = current.Total() - prev.Total();
			m_Cpu = Share(current.Busy() - prev.Busy(), total);
			m_KernelCpu = Share(current.System + current.Irq + current.SoftIrq - prev.System - prev.Irq - prev.SoftIrq, total);
			m_ProcessorCpu.resize(cpus.size() - 1);
			for (size_t i = 1; i < cpus.size(); i++)
				m_ProcessorCpu[i - 1] = Share(cpus[i].Busy() - m_Prev[i].Busy(), cpus[i].Total() - m_Prev[i].Total());
		}
		else {
			m_Cpu = m_KernelCpu = 0;
			m_ProcessorCpu.assign(cpus.size() - 1, 0);
		}
		m_Prev = std::move(cpus);

		std::unordered_map<std::string, DiskCounters> diskCounters;
		for (auto& counters : ProcFs::ParseDiskStats(sources.DiskStats))
			diskCounters.emplace(counters.Name, counters);
		m_Disks.clear();
		for (auto& disk : disks) {
			auto it = diskCounters.find(disk.Name);
			if (it == diskCounters.end())
				continue;
			auto& current = it->second;
			DiskRate rate{ disk };
			rate.TotalRead = current.SectorsRead * ProcFs::SectorSize;
			rate.TotalWritten = current.SectorsWritten * ProcFs::SectorSize;
			if (auto prev = m_PrevDisks.find(disk.Name); prev != m_PrevDisks.end() && elapsed > 0) {
				rate.ReadRate = PerSecond(current.SectorsRead, prev->second.SectorsRead, elapsed) * ProcFs::SectorSize;
				rate.WriteRate = PerSecond(current.SectorsWritten, prev->second.SectorsWritten, elapsed) * ProcFs::SectorSize;
				if (current.IoMilliseconds >= prev->second.IoMilliseconds) {
					// milliseconds busy over nanoseconds elapsed, in 1/10000 of a percent
					auto active = (current.IoMilliseconds - prev->second.IoMilliseconds) * 1000000000000ull / elapsed;
					rate.Active = static_cast<int32_t>(std::min<uint64_t>(active, 1000000));
				}
			}
			m_Disks.push_back(std::move(rate));
		}
		m_PrevDisks = std::move(diskCounters);

		std::unordered_map<std::string, NetworkCounters> networkCounters;
		m_Networks.clear();
		for (auto& current : ProcFs::ParseNetDev(sources.NetDev)) {
			NetworkRate rate{ current.Name };
			rate.TotalReceived = current.ReceivedBytes;
			rate.TotalSent = current.SentBytes;
			if (auto prev = m_PrevNetworks.find(current.Name); prev != m_PrevNetworks.end()) {
				rate.ReceiveRate = PerSecond(current.ReceivedBytes, prev->second.ReceivedBytes, elapsed);
				rate.SendRate = PerSecond(current.SentBytes, prev->second.SentBytes, elapsed);
			}
			m_Networks.push_back(std::move(rate));
			networkCounters.emplace(current.Name, current);
		}
		m_PrevNetworks = std::move(networkCounters);
		return true;
	}
}
