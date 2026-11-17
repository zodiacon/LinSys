#include "TestFramework.h"
#include "FakeProc.h"
#include <LinSys/Disks.h>
#include <LinSys/ProcessDetails.h>

#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace LinSys;
namespace fs = std::filesystem;
using namespace std::string_view_literals;

// lines as WSL's kernel writes them
TEST(ProcFs_ParseCpuModel) {
	CHECK_EQ(ProcFs::ParseCpuModel("processor\t: 0\nvendor_id\t: GenuineIntel\nmodel name\t: Intel(R) Core(TM) i9-9980HK CPU @ 2.40GHz\nprocessor\t: 1\nmodel name\t: other\n"),
		std::string("Intel(R) Core(TM) i9-9980HK CPU @ 2.40GHz"));
	// a Raspberry Pi's: no model name
	CHECK_EQ(ProcFs::ParseCpuModel("processor\t: 0\nBogoMIPS\t: 108.00\nHardware\t: BCM2835\nModel\t: Raspberry Pi 4\n"), std::string("BCM2835"));
	CHECK_EQ(ProcFs::ParseCpuModel(""), std::string());
}

TEST(ProcFs_ParseMountInfo) {
	auto mounts = ProcFs::ParseMountInfo(
		"78 82 0:34 / /mnt/wsl rw,relatime shared:1 - tmpfs none rw\n"
		"82 67 8:48 / / rw,relatime - ext4 /dev/sdd rw,discard,errors=remount-ro,data=ordered\n"
		"90 82 8:49 /data /srv/my\\040files rw master:3 shared:7 - ext4 /dev/sdd1 rw\n"
		"garbage\n");
	CHECK_EQ(mounts.size(), 3u);
	if (mounts.size() != 3)
		return;
	CHECK_EQ(mounts[0].Major, 0u);
	CHECK_EQ(mounts[0].FileSystem, std::string("tmpfs"));
	CHECK_EQ(mounts[1].Major, 8u);
	CHECK_EQ(mounts[1].Minor, 48u);
	CHECK_EQ(mounts[1].MountPoint, std::string("/"));
	CHECK_EQ(mounts[1].Source, std::string("/dev/sdd"));
	CHECK_EQ(mounts[2].Root, std::string("/data"));
	CHECK_EQ(mounts[2].MountPoint, std::string("/srv/my files"));	// \040 decoded; optional fields skipped
	CHECK_EQ(mounts[2].FileSystem, std::string("ext4"));
}

TEST(ProcFs_ParseMaps) {
	auto maps = ProcFs::ParseMaps(
		"5c2a60d36000-5c2a60e2a000 r--p 00000000 08:30 1724                       /usr/lib/cargo/bin/coreutils/head\n"
		"Size:                976 kB\n"
		"Rss:                 976 kB\n"
		"Pss:                 488 kB\n"
		"Pss_Dirty:             8 kB\n"
		"Swap:                 12 kB\n"
		"SwapPss:               4 kB\n"
		"THPeligible:           0\n"
		"VmFlags: rd mr mw me sd\n"
		"7ffd1c000000-7ffd1c021000 rw-p 00000000 00:00 0                          [stack]\n"
		"7f0000000000-7f0000001000 rw-s 00001000 00:05 99 /memfd:my file (deleted)\n"
		"7f0000002000-7f0000003000 ---p 00000000 00:00 0 \n");
	CHECK_EQ(maps.size(), 4u);
	if (maps.size() != 4)
		return;
	CHECK_EQ(maps[0].Start, 0x5c2a60d36000ull);
	CHECK_EQ(maps[0].End, 0x5c2a60e2a000ull);
	CHECK_EQ(maps[0].Permissions, std::string("r--p"));
	CHECK_EQ(maps[0].Device, std::string("08:30"));
	CHECK_EQ(maps[0].Inode, 1724ull);
	CHECK_EQ(maps[0].Path, std::string("/usr/lib/cargo/bin/coreutils/head"));
	CHECK_EQ(maps[0].Resident, 976ull * 1024);
	CHECK_EQ(maps[0].Proportional, 488ull * 1024);	// not Pss_Dirty
	CHECK_EQ(maps[0].Swap, 12ull * 1024);			// not SwapPss
	CHECK_EQ(maps[1].Path, std::string("[stack]"));
	CHECK_EQ(maps[1].Resident, 0ull);				// maps' lines: no sizes
	CHECK_EQ(maps[2].Offset, 0x1000ull);
	CHECK_EQ(maps[2].Path, std::string("/memfd:my file (deleted)"));	// spaces kept
	CHECK_EQ(maps[3].Path, std::string());			// anonymous
}

TEST(ProcFs_ParseFdInfo) {
	auto info = ProcFs::ParseFdInfo("pos:\t4096\nflags:\t02100002\nmnt_id:\t16\nino:\t31856\n");
	CHECK_EQ(info.Position, 4096ull);
	// the kernel's values: its O_LARGEFILE (0100000) is 0 in a 64-bit program's headers
	CHECK_EQ(info.Flags, 02100002u);
	CHECK_EQ(info.Flags & O_ACCMODE, static_cast<uint32_t>(O_RDWR));
	CHECK_EQ(ProcFs::ParseFdInfo("pos:\t0\nflags:\t00\n").Flags, 0u);
}

TEST(ProcFs_ClassifyFdTarget) {
	CHECK(ProcFs::ClassifyFdTarget("socket:[123]") == FileKind::Socket);
	CHECK(ProcFs::ClassifyFdTarget("pipe:[31856]") == FileKind::Pipe);
	CHECK(ProcFs::ClassifyFdTarget("anon_inode:[eventfd]") == FileKind::AnonymousInode);
	CHECK(ProcFs::ClassifyFdTarget("/dev/ptmx") == FileKind::Device);
	CHECK(ProcFs::ClassifyFdTarget("/home/pavel/a.txt") == FileKind::File);
	CHECK(ProcFs::ClassifyFdTarget("net:[4026531840]") == FileKind::Other);
}

TEST(ProcFs_ParseEnviron) {
	auto variables = ProcFs::ParseEnviron("PATH=/usr/bin:/bin\0EMPTY=\0EQUALS=a=b\0ODD\0"sv);
	CHECK_EQ(variables.size(), 4u);
	if (variables.size() != 4)
		return;
	CHECK_EQ(variables[0].first, std::string("PATH"));
	CHECK_EQ(variables[0].second, std::string("/usr/bin:/bin"));
	CHECK_EQ(variables[1].second, std::string());
	CHECK_EQ(variables[2].second, std::string("a=b"));	// split at the first '='
	CHECK_EQ(variables[3].first, std::string("ODD"));
}

TEST(Disks_DiskOfDeviceLink) {
	CHECK_EQ(Disks::DiskOfDeviceLink("../../devices/LNXSYSTM:00/LNXSYBUS:00/ACPI0004:00/MSFT1000:00/fd1d/host0/target0:0:0/0:0:0:3/block/sdd"),
		std::string("sdd"));
	CHECK_EQ(Disks::DiskOfDeviceLink("../../devices/pci0000:00/0000:00:1d.0/nvme/nvme0/nvme0n1/block/nvme0n1/nvme0n1p2"), std::string("nvme0n1"));
	CHECK_EQ(Disks::DiskOfDeviceLink("../../devices/virtual/block/dm-0"), std::string("dm-0"));
	CHECK_EQ(Disks::DiskOfDeviceLink("nothing"), std::string());
}

// a disk's mount points from mountinfo, through /sys/dev/block's links: its own and its partitions', whole file
// systems only, shortest first
TEST(Disks_MountPoints) {
	Tests::FakeProc proc, sys;
	auto addDisk = [&](const char* name) {
		fs::create_directories(sys.Root() + "/block/" + name);
		std::ofstream(sys.Root() + "/block/" + name + "/size") << "1000\n";
	};
	addDisk("sda");
	addDisk("sdb");
	fs::create_directories(sys.Root() + "/dev/block");
	fs::create_symlink("../../devices/pci/block/sda", sys.Root() + "/dev/block/8:0");
	fs::create_symlink("../../devices/pci/block/sda/sda2", sys.Root() + "/dev/block/8:2");
	fs::create_directories(proc.Root() + "/self");
	std::ofstream(proc.Root() + "/self/mountinfo") <<
		"1 0 8:2 / /home rw - ext4 /dev/sda2 rw\n"
		"2 0 8:0 / / rw - ext4 /dev/sda rw\n"
		"3 0 8:0 /var/lib /srv rw - ext4 /dev/sda rw\n"	// a bind mount of a folder: left out
		"4 0 0:20 / /tmp rw - tmpfs none rw\n"
		"5 0 8:2 / /home rw - ext4 /dev/sda2 rw\n";		// mounted twice: once in the list

	auto disks = Disks::List(sys.Root(), proc.Root());
	CHECK_EQ(disks.size(), 2u);
	if (disks.size() != 2)
		return;
	CHECK_EQ(disks[0].MountPoints.size(), 2u);
	if (disks[0].MountPoints.size() == 2) {
		CHECK_EQ(disks[0].MountPoints[0], std::string("/"));
		CHECK_EQ(disks[0].MountPoints[1], std::string("/home"));
	}
	CHECK(disks[1].MountPoints.empty());
}

namespace {
	// a fake process with open files, maps and an environment
	void AddDetails(Tests::FakeProc& proc, uint32_t pid) {
		auto dir = proc.Root() + "/" + std::to_string(pid);
		fs::create_directories(dir + "/fd");
		fs::create_directories(dir + "/fdinfo");
		fs::create_symlink("pipe:[31856]", dir + "/fd/0");
		fs::create_symlink("/home/user/notes.txt", dir + "/fd/10");
		fs::create_symlink("socket:[999]", dir + "/fd/3");
		std::ofstream(dir + "/fdinfo/10") << "pos:\t120\nflags:\t02000002\n";
		std::ofstream(dir + "/maps") << "00400000-00401000 r-xp 00000000 08:01 7 /usr/bin/app\n";
		std::ofstream(dir + "/environ") << std::string("HOME=/home/user\0LANG=C\0"sv);
		fs::create_symlink("/home/user", dir + "/cwd");
	}
}

TEST(ProcessDetails_Fake) {
	Tests::FakeProc proc;
	proc.SetProcess(100, "app", 1, 0, 0, 50);
	AddDetails(proc, 100);
	ProcessOrThreadKey key{ 50, 100 };

	std::vector<OpenFile> files;
	CHECK_EQ(ProcessDetails::GetOpenFiles(key, files, proc.Root()), 0);
	CHECK_EQ(files.size(), 3u);
	if (files.size() == 3) {
		CHECK_EQ(files[0].Fd, 0);	// by number: 0, 3, 10
		CHECK(files[0].Kind == FileKind::Pipe);
		CHECK_EQ(files[1].Fd, 3);
		CHECK_EQ(files[2].Target, std::string("/home/user/notes.txt"));
		CHECK_EQ(files[2].Position, 120ull);
		CHECK_EQ(files[2].Flags, static_cast<uint32_t>(O_RDWR | O_CLOEXEC));
	}

	std::vector<MemoryMap> maps;
	CHECK_EQ(ProcessDetails::GetMemoryMaps(key, maps, proc.Root()), 0);	// no smaps: maps
	CHECK_EQ(maps.size(), 1u);

	std::vector<std::pair<std::string, std::string>> variables;
	CHECK_EQ(ProcessDetails::GetEnvironment(key, variables, proc.Root()), 0);
	CHECK_EQ(variables.size(), 2u);

	std::string cwd;
	CHECK_EQ(ProcessDetails::GetCurrentDirectory(key, cwd, proc.Root()), 0);
	CHECK_EQ(cwd, std::string("/home/user"));
}

// the id is another process's now: nothing, ESRCH
TEST(ProcessDetails_ReusedId) {
	Tests::FakeProc proc;
	proc.SetProcess(100, "new", 1, 0, 0, 70);
	AddDetails(proc, 100);
	ProcessOrThreadKey old{ 50, 100 };
	std::vector<OpenFile> files;
	CHECK_EQ(ProcessDetails::GetOpenFiles(old, files, proc.Root()), ESRCH);
	CHECK(files.empty());
	std::vector<std::pair<std::string, std::string>> variables;
	CHECK_EQ(ProcessDetails::GetEnvironment(old, variables, proc.Root()), ESRCH);
	CHECK(variables.empty());
	std::vector<MemoryMap> maps;
	CHECK_EQ(ProcessDetails::GetMemoryMaps(old, maps, proc.Root()), ESRCH);

	std::vector<OpenFile> none;
	CHECK_EQ(ProcessDetails::GetOpenFiles({ 1, 12345 }, none, proc.Root()), ENOENT);	// no such process
}

// this process: a file it opens, its executable among the maps (with sizes), its environment
TEST(ProcessDetails_Live) {
	auto text = ProcFs::ReadFile("/proc/self/stat");
	auto stat = text ? ProcFs::ParseStat(*text) : std::nullopt;
	CHECK(stat.has_value());
	if (!stat)
		return;
	ProcessOrThreadKey self{ static_cast<int64_t>(stat->StartTime), stat->Pid };

	int fd = ::open("/proc/self/stat", O_RDONLY | O_CLOEXEC);
	std::vector<OpenFile> files;
	CHECK_EQ(ProcessDetails::GetOpenFiles(self, files), 0);
	bool found = false;
	for (auto& file : files)
		if (file.Fd == fd)
			found = file.Kind == FileKind::File && (file.Flags & O_CLOEXEC);
	CHECK(found);
	::close(fd);

	std::vector<MemoryMap> maps;
	CHECK_EQ(ProcessDetails::GetMemoryMaps(self, maps), 0);
	auto exe = ProcFs::ReadLink("/proc/self/exe").value_or("?");
	bool exeMapped = false;
	for (auto& map : maps)
		if (map.Path == exe && map.Resident > 0)
			exeMapped = true;
	CHECK(exeMapped);

	std::vector<std::pair<std::string, std::string>> variables;
	CHECK_EQ(ProcessDetails::GetEnvironment(self, variables), 0);
	CHECK(!variables.empty());

	CHECK(!SystemInfo::GetCpuModel().empty());
	// the root file system's disk has "/" among its mount points (where / is on a disk at all)
	for (auto& disk : Disks::List())
		for (auto& point : disk.MountPoints)
			CHECK(point.starts_with('/'));
}
