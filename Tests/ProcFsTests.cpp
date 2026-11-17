#include "TestFramework.h"
#include <LinSys/ProcFs.h>


using namespace LinSys;

// a real line (kernel 6.18), all 52 fields
TEST(ProcFs_ParseStat) {
	auto stat = ProcFs::ParseStat("1234 (bash) S 1200 1234 1234 34816 5678 4194304 2047 9512 0 3 15 7 21 9 20 0 1 0 "
		"88123 9101312 1290 18446744073709551615 94000 95000 140000 0 0 0 65536 3670020 1266777851 0 0 0 17 5 0 0 0 0 0 "
		"96000 97000 98000 140001 140002 140003 140004 0\n");
	CHECK(stat.has_value());
	if (!stat)
		return;
	CHECK_EQ(stat->Pid, 1234u);
	CHECK_EQ(stat->Comm, std::string("bash"));
	CHECK_EQ(stat->State, 'S');
	CHECK_EQ(stat->ParentPid, 1200u);
	CHECK_EQ(stat->Flags, 4194304u);
	CHECK((stat->Flags & ProcFs::KernelThreadFlag) == 0);
	CHECK_EQ(stat->UserTime, 15ull);
	CHECK_EQ(stat->KernelTime, 7ull);
	CHECK_EQ(stat->Priority, 20ll);
	CHECK_EQ(stat->Nice, 0ll);
	CHECK_EQ(stat->ThreadCount, 1u);
	CHECK_EQ(stat->StartTime, 88123ull);
	CHECK_EQ(stat->VirtualSize, 9101312ull);
	CHECK_EQ(stat->ResidentPages, 1290ull);
	CHECK_EQ(stat->Processor, 5);
}

// the name can hold spaces and parentheses: it ends at the last ')'
TEST(ProcFs_ParseStatNameWithParentheses) {
	auto stat = ProcFs::ParseStat("77 (a) b (c)) R 1 77 77 0 -1 0 0 0 0 0 100 200 0 0 39 19 4 0 500 1000 10");
	CHECK(stat.has_value());
	if (!stat)
		return;
	CHECK_EQ(stat->Comm, std::string("a) b (c)"));
	CHECK_EQ(stat->State, 'R');
	CHECK_EQ(stat->Nice, 19ll);
	CHECK_EQ(stat->ThreadCount, 4u);
	CHECK_EQ(stat->Processor, -1);	// not in this (short) line
}

TEST(ProcFs_ParseStatMalformed) {
	CHECK(!ProcFs::ParseStat(""));
	CHECK(!ProcFs::ParseStat("12 bash S 1"));
	CHECK(!ProcFs::ParseStat("x (bash) S 1 1 1 0 -1 0 0 0 0 0 1 1 0 0 20 0 1 0 5 100 1"));
	CHECK(!ProcFs::ParseStat("12 (bash) S 1 1 1 0 -1 0 0 0 0 0 1 1 0 0 20 0 1 0"));	// no starttime
	CHECK(!ProcFs::ParseStat("12 (bash) S 1 1 1 0 -1 0 0 0 0 0 x 1 0 0 20 0 1 0 5 100 1"));
}

TEST(ProcFs_ParseStatusUid) {
	CHECK_EQ(ProcFs::ParseStatusUid("Name:\tbash\nUmask:\t0022\nState:\tS (sleeping)\nUid:\t1000\t1000\t1000\t1000\nGid:\t1000\t1000\t1000\t1000\n").value_or(-1), 1000u);
	CHECK(!ProcFs::ParseStatusUid("Name:\tbash\n"));
}

TEST(ProcFs_ParseCpuTimes) {
	auto cpus = ProcFs::ParseCpuTimes(
		"cpu  100 5 50 1000 20 3 2 1 0 0\n"
		"cpu0 60 5 30 500 10 2 1 1 0 0\n"
		"cpu1 40 0 20 500 10 1 1 0 0 0\n"
		"intr 12345 0 0\n"
		"ctxt 999\n");
	CHECK_EQ(cpus.size(), 3u);
	if (cpus.size() != 3)
		return;
	CHECK_EQ(cpus[0].User, 100ull);
	CHECK_EQ(cpus[0].Steal, 1ull);
	CHECK_EQ(cpus[0].Busy(), 161ull);
	CHECK_EQ(cpus[0].Total(), 1181ull);
	CHECK_EQ(cpus[2].System, 20ull);
}

TEST(ProcFs_ParseMemInfo) {
	auto info = ProcFs::ParseMemInfo(
		"MemTotal:       16303680 kB\n"
		"MemFree:         9876543 kB\n"
		"MemAvailable:   12000000 kB\n"
		"Buffers:           12345 kB\n"
		"Cached:          1234567 kB\n"
		"SwapCached:            0 kB\n"
		"SwapTotal:       4194304 kB\n"
		"SwapFree:        4194304 kB\n");
	CHECK(info.has_value());
	if (!info)
		return;
	CHECK_EQ(info->Total, 16303680ull * 1024);
	CHECK_EQ(info->Available, 12000000ull * 1024);
	CHECK_EQ(info->Cached, 1234567ull * 1024);	// not SwapCached
	CHECK_EQ(info->SwapFree, 4194304ull * 1024);
	CHECK(!ProcFs::ParseMemInfo("MemFree: 1 kB\n"));
}

TEST(ProcFs_ParseCommandLine) {
	using namespace std::string_view_literals;
	CHECK_EQ(ProcFs::ParseCommandLine("/usr/bin/vim\0-u\0NONE\0"sv), std::string("/usr/bin/vim -u NONE"));
	CHECK_EQ(ProcFs::ParseCommandLine(""sv), std::string());
}

// as WSL's Ubuntu shows them to a user without root
TEST(ProcFs_ImageNameFromCommandLine) {
	using namespace std::string_view_literals;
	// a script: argv[1]
	CHECK_EQ(ProcFs::ImageNameFromCommandLine("/usr/bin/python3\0/usr/share/unattended-upgrades/unattended-upgrade-shutdown\0--wait-for-signal\0"sv, "unattended-upgr"),
		std::string("unattended-upgrade-shutdown"));
	CHECK_EQ(ProcFs::ImageNameFromCommandLine("/usr/bin/python3\0/usr/bin/networkd-dispatcher\0--run-startup-triggers\0"sv, "networkd-dispat"),
		std::string("networkd-dispatcher"));
	// argv[0]
	CHECK_EQ(ProcFs::ImageNameFromCommandLine("/usr/lib/systemd/systemd-journald\0"sv, "systemd-journal"), std::string("systemd-journald"));
	// rewritten arguments
	CHECK_EQ(ProcFs::ImageNameFromCommandLine("gnome-session-binary: running\0"sv, "gnome-session-b"), std::string("gnome-session-binary:"));
	// comm wasn't cut: it's the name, whatever the command line says
	CHECK_EQ(ProcFs::ImageNameFromCommandLine("/usr/bin/python3\0/usr/bin/foo\0"sv, "foo"), std::string("foo"));
	// nothing starts with it
	CHECK_EQ(ProcFs::ImageNameFromCommandLine("/usr/bin/python3\0/usr/bin/other\0"sv, "a-long-name-cut"), std::string("a-long-name-cut"));
	CHECK_EQ(ProcFs::ImageNameFromCommandLine(""sv, "a-long-name-cut"), std::string("a-long-name-cut"));
}

// lines as WSL's kernel (6.18) writes them: 17 fields after the name (older kernels have 11 or 15)
TEST(ProcFs_ParseDiskStats) {
	auto disks = ProcFs::ParseDiskStats(
		"   1       0 ram0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
		"   8       0 sda 2731 0 365552 1187 0 0 0 0 0 1580 1187 0 0 0 0 0 0\n"
		"   8      48 sdd 52121 18837 3964454 21533 98213 101917 6352032 236718 0 104720 268102 0 0 0 0 1530 9850\n"
		" 259       1 nvme0n1p1 100 0 200 5 10 0 80 1 0 7 6\n"
		"   8      64 bad 1 2 x\n");
	CHECK_EQ(disks.size(), 4u);
	if (disks.size() != 4)
		return;
	CHECK_EQ(disks[1].Name, std::string("sda"));
	CHECK_EQ(disks[1].Reads, 2731ull);
	CHECK_EQ(disks[1].SectorsRead, 365552ull);
	CHECK_EQ(disks[1].IoMilliseconds, 1580ull);
	CHECK_EQ(disks[2].Writes, 98213ull);
	CHECK_EQ(disks[2].SectorsWritten, 6352032ull);
	CHECK_EQ(disks[2].IoMilliseconds, 104720ull);
	CHECK_EQ(disks[3].Name, std::string("nvme0n1p1"));	// 11 fields
	CHECK_EQ(disks[3].IoMilliseconds, 7ull);
}

TEST(ProcFs_ParseNetDev) {
	auto interfaces = ProcFs::ParseNetDev(
		"Inter-|   Receive                                                |  Transmit\n"
		" face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
		"    lo:   13855     118    0    0    0     0          0         0    13855     118    0    0    0     0       0          0\n"
		"  eth0: 48211098    3874    0    0    0     0          0       408   247767    2735    0    0    0     0       0          0\n"
		"wlan0:1000 10 0 0 0 0 0 0 2000 20 0 0 0 0 0 0\n");
	CHECK_EQ(interfaces.size(), 3u);
	if (interfaces.size() != 3)
		return;
	CHECK_EQ(interfaces[0].Name, std::string("lo"));
	CHECK_EQ(interfaces[1].Name, std::string("eth0"));
	CHECK_EQ(interfaces[1].ReceivedBytes, 48211098ull);
	CHECK_EQ(interfaces[1].ReceivedPackets, 3874ull);
	CHECK_EQ(interfaces[1].SentBytes, 247767ull);
	CHECK_EQ(interfaces[1].SentPackets, 2735ull);
	CHECK_EQ(interfaces[2].Name, std::string("wlan0"));	// no space after the colon
	CHECK_EQ(interfaces[2].SentBytes, 2000ull);
}
