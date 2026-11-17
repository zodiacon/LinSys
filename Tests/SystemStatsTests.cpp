#include "TestFramework.h"
#include <LinSys/Disks.h>
#include <LinSys/SystemStats.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace LinSys;
namespace fs = std::filesystem;

namespace {
	constexpr int64_t Second = 1000000000;
	const char* Memory = "MemTotal: 1000 kB\nMemAvailable: 400 kB\n";
	const char* Cpu = "cpu  100 0 100 800 0 0 0 0\ncpu0 100 0 100 800 0 0 0 0\n";

	std::string DiskLine(const char* name, uint64_t sectorsRead, uint64_t sectorsWritten, uint64_t ioMs) {
		return "8 0 " + std::string(name) + " 10 0 " + std::to_string(sectorsRead) + " 0 10 0 " + std::to_string(sectorsWritten)
			+ " 0 0 " + std::to_string(ioMs) + " 0 0 0 0 0 0 0\n";
	}
	std::string NetLine(const char* name, uint64_t received, uint64_t sent) {
		return "  " + std::string(name) + ": " + std::to_string(received) + " 1 0 0 0 0 0 0 " + std::to_string(sent) + " 1 0 0 0 0 0 0\n";
	}
}

TEST(SystemStats_Cpu) {
	SystemStats stats;
	CHECK(stats.UpdateFrom({ "cpu  100 0 100 800 0 0 0 0\ncpu0 50 0 50 400 0 0 0 0\ncpu1 50 0 50 400 0 0 0 0\n", Memory, "", "" }, Second, {}));
	CHECK_EQ(stats.GetCpu(), 0);	// one sample
	// 400 ticks more: cpu0 busy for 150 of 200, cpu1 for 50 of 200
	CHECK(stats.UpdateFrom({ "cpu  250 0 150 1000 0 0 0 0\ncpu0 150 0 100 450 0 0 0 0\ncpu1 100 0 50 550 0 0 0 0\n", Memory, "", "" }, 2 * Second, {}));
	CHECK_EQ(stats.GetCpu(), 500000);		// 200 of 400: 50%
	CHECK_EQ(stats.GetKernelCpu(), 125000);	// 50 of 400
	CHECK_EQ(stats.GetProcessorCpu().size(), 2u);
	if (stats.GetProcessorCpu().size() == 2) {
		CHECK_EQ(stats.GetProcessorCpu()[0], 750000);
		CHECK_EQ(stats.GetProcessorCpu()[1], 250000);
	}
	CHECK_EQ(stats.GetMemory().Available, 400ull * 1024);
}

TEST(SystemStats_Disks) {
	SystemStats stats;
	std::vector<DiskInfo> disks{ { "sda", "Virtual Disk", 1 << 30 }, { "sdb", "", 1 << 20 } };
	// sda1 (a partition) and loop0 aren't in the list: left out
	auto first = DiskLine("sda", 1000, 2000, 100) + DiskLine("sda1", 900, 1900, 90) + DiskLine("loop0", 5, 5, 5) + DiskLine("sdb", 0, 0, 0);
	CHECK(stats.UpdateFrom({ Cpu, Memory, first, "" }, Second, disks));
	CHECK_EQ(stats.GetDisks().size(), 2u);
	CHECK_EQ(stats.GetDisks()[0].ReadRate, 0ull);	// one sample
	CHECK_EQ(stats.GetDisks()[0].TotalRead, 1000ull * 512);

	// two seconds on: 2048 sectors read (512 KB a second), 4096 written, 500 ms busy (25%)
	auto second = DiskLine("sda", 3048, 6096, 600) + DiskLine("sdb", 0, 0, 0);
	CHECK(stats.UpdateFrom({ Cpu, Memory, second, "" }, 3 * Second, disks));
	auto& sda = stats.GetDisks()[0];
	CHECK_EQ(sda.Disk.Name, std::string("sda"));
	CHECK_EQ(sda.Disk.Model, std::string("Virtual Disk"));
	CHECK_EQ(sda.ReadRate, 512ull * 1024);
	CHECK_EQ(sda.WriteRate, 1024ull * 1024);
	CHECK_EQ(sda.Active, 250000);
	CHECK_EQ(stats.GetDisks()[1].Active, 0);

	// a disk that was reset (counters back to 0): no rate, not a huge one
	CHECK(stats.UpdateFrom({ Cpu, Memory, DiskLine("sda", 10, 10, 1), "" }, 4 * Second, disks));
	CHECK_EQ(stats.GetDisks()[0].ReadRate, 0ull);
	CHECK_EQ(stats.GetDisks()[0].Active, 0);
	CHECK_EQ(stats.GetDisks().size(), 1u);	// sdb is gone from diskstats
}

TEST(SystemStats_Networks) {
	SystemStats stats;
	CHECK(stats.UpdateFrom({ Cpu, Memory, "", NetLine("lo", 100, 100) + NetLine("eth0", 1000, 500) }, Second, {}));
	CHECK_EQ(stats.GetNetworks().size(), 2u);
	CHECK_EQ(stats.GetNetworks()[1].ReceiveRate, 0ull);
	// half a second on
	CHECK(stats.UpdateFrom({ Cpu, Memory, "", NetLine("lo", 100, 100) + NetLine("eth0", 101000, 1500) + NetLine("wg0", 7, 7) }, Second + Second / 2, {}));
	auto& networks = stats.GetNetworks();
	CHECK_EQ(networks.size(), 3u);
	if (networks.size() == 3) {
		CHECK_EQ(networks[1].Name, std::string("eth0"));
		CHECK_EQ(networks[1].ReceiveRate, 200000ull);
		CHECK_EQ(networks[1].SendRate, 2000ull);
		CHECK_EQ(networks[1].TotalReceived, 101000ull);
		CHECK_EQ(networks[2].ReceiveRate, 0ull);	// new: no rate yet
	}
}

TEST(Disks_List) {
	char path[] = "/tmp/linsys-sys-XXXXXX";
	std::string root = ::mkdtemp(path);
	auto add = [&](const char* name, const char* size, const char* model) {
		auto dir = root + "/block/" + name;
		fs::create_directories(dir + "/device");
		std::ofstream(dir + "/size") << size << "\n";
		if (model)
			std::ofstream(dir + "/device/model") << model;
	};
	add("sdb", "326464", "Virtual Disk    \n");
	add("nvme0n1", "1000215216", "Samsung SSD 980 PRO 1TB\n");
	add("loop0", "0", nullptr);
	add("loop1", "2048", nullptr);
	add("ram0", "8192", nullptr);
	add("dm-0", "500000", nullptr);
	add("sr0", "0", "DVD-ROM\n");
	add("mmcblk0", "62333952", nullptr);

	auto disks = Disks::List(root);
	fs::remove_all(root);
	CHECK_EQ(disks.size(), 3u);
	if (disks.size() != 3)
		return;
	CHECK_EQ(disks[0].Name, std::string("mmcblk0"));
	CHECK_EQ(disks[0].Model, std::string());
	CHECK_EQ(disks[1].Name, std::string("nvme0n1"));
	CHECK_EQ(disks[1].Size, 1000215216ull * 512);
	CHECK_EQ(disks[1].Model, std::string("Samsung SSD 980 PRO 1TB"));
	CHECK_EQ(disks[2].Model, std::string("Virtual Disk"));
}

TEST(SystemStats_Live) {
	SystemStats stats;
	CHECK(stats.Update());
	CHECK(stats.GetMemory().Total > 0);
	CHECK(stats.GetMemory().Available <= stats.GetMemory().Total);
	CHECK_EQ(stats.GetProcessorCpu().size(), static_cast<size_t>(ProcFs::ProcessorCount()));
	CHECK(!stats.GetNetworks().empty());	// lo, at least
	for (auto& disk : stats.GetDisks())
		CHECK(disk.Disk.Size > 0);
}
