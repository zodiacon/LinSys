#include "TestFramework.h"
#include <LinSys/ProcFs.h>
#include <LinSys/Services.h>

using namespace LinSys;

TEST(Services_UnitNameOfPath) {
	CHECK_EQ(ServiceManager::UnitNameOfPath("/usr/lib/systemd/system/ssh.service"), std::string("ssh.service"));
	CHECK_EQ(ServiceManager::UnitNameOfPath("ssh.service"), std::string("ssh.service"));
}

// loaded units with their unit files' states and main processes; installed ones not loaded added as stopped;
// templates left out; by name
TEST(Services_Merge) {
	std::vector<UnitEntry> units{
		{ "ssh.service", "OpenBSD Secure Shell server", "loaded", "active", "running", "/org/freedesktop/systemd1/unit/ssh_2eservice" },
		{ "apport.service", "Automatic crash report generation", "loaded", "failed", "failed", "/x" },
		{ "gone.service", "gone.service", "not-found", "inactive", "dead", "/y" },	// referenced, no unit file
	};
	std::unordered_map<std::string, std::string> unitFiles{
		{ "ssh.service", "enabled" }, { "apport.service", "disabled" }, { "cron.service", "enabled" },
		{ "getty@.service", "enabled" },	// a template
	};
	std::unordered_map<std::string, uint32_t> pids{ { "ssh.service", 812 } };

	auto services = ServiceManager::Merge(units, unitFiles, pids);
	CHECK_EQ(services.size(), 4u);
	if (services.size() != 4)
		return;
	CHECK_EQ(services[0].Name, std::string("apport.service"));
	CHECK_EQ(services[0].ActiveState, std::string("failed"));
	CHECK_EQ(services[0].UnitFileState, std::string("disabled"));
	CHECK_EQ(services[1].Name, std::string("cron.service"));	// installed, not loaded
	CHECK_EQ(services[1].LoadState, std::string());
	CHECK_EQ(services[1].ActiveState, std::string("inactive"));
	CHECK_EQ(services[1].UnitFileState, std::string("enabled"));
	CHECK_EQ(services[2].Name, std::string("gone.service"));
	CHECK_EQ(services[2].UnitFileState, std::string());
	CHECK_EQ(services[3].MainPid, 812u);
	CHECK_EQ(services[3].Description, std::string("OpenBSD Secure Shell server"));
}

// the machine's systemd: services listed, running ones' main processes exist; a call that fails says why
TEST(Services_Live) {
	ServiceManager manager;
	CHECK_EQ(manager.GetConnectError(), 0);
	std::vector<ServiceInfo> services;
	auto result = manager.List(services, false);
	CHECK_EQ(result.Error, 0);
	CHECK(!services.empty());
	bool running = false;
	for (auto& service : services) {
		if (service.MainPid) {
			running = true;
			CHECK(ProcFs::ReadFile("/proc/" + std::to_string(service.MainPid) + "/stat").has_value());
		}
	}
	CHECK(running);

	// no such unit (or, without root, not allowed): an error either way, with systemd's words
	auto start = manager.Start("linsys-no-such-unit-4711.service");
	CHECK(start.Error != 0);
	CHECK(!start.Message.empty());
}
