#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct sd_bus;

namespace LinSys {
	// a systemd service: loaded (systemd has it in memory: running, failed, or stopped but referenced) or only
	// installed (a unit file nothing has loaded)
	struct ServiceInfo {
		std::string Name;				// "ssh.service"
		std::string Description;		// empty for one not loaded
		std::string LoadState;			// loaded, not-found, masked, error; "" for one not loaded
		std::string ActiveState;		// active, inactive, failed, activating, deactivating, reloading
		std::string SubState;			// running, exited, dead, failed, start...
		std::string UnitFileState;		// enabled, disabled, static, masked, generated, alias...; "" if no unit file
		uint32_t MainPid{};				// 0 if none (stopped, or a oneshot)
	};

	// ListUnitsByPatterns' entry
	struct UnitEntry {
		std::string Name, Description, LoadState, ActiveState, SubState, ObjectPath;
	};

	// a call's result: 0, or an errno value with systemd's message ("Access denied", "Unit x.service not found.")
	struct BusResult {
		int Error{};
		std::string Message;
	};

	// systemd's services over D-Bus (sd-bus): one connection, used from one thread. Listing the loaded units is quick
	// (a few ms); the unit files are slow (systemd reads its folders: 150 ms here), so they're read when asked
	class ServiceManager {
	public:
		enum class Bus { System, User };

		explicit ServiceManager(Bus bus = Bus::System);
		~ServiceManager();
		ServiceManager(const ServiceManager&) = delete;
		ServiceManager& operator=(const ServiceManager&) = delete;

		// connecting failed: why (an errno value; the calls then return it)
		[[nodiscard]] int GetConnectError() const { return m_ConnectError; }

		// the services, by name: the loaded ones and the installed ones not loaded (templates, "x@.service",
		// left out: they aren't services themselves). refreshUnitFiles: read the unit files again (else the last ones)
		[[nodiscard]] BusResult List(std::vector<ServiceInfo>& services, bool refreshUnitFiles);

		// a job is queued ("replace": it replaces a conflicting one); its result comes as the state changes.
		// Without root, polkit decides (an agent may ask for a password; with none, access is denied: EACCES)
		[[nodiscard]] BusResult Start(const std::string& name);
		[[nodiscard]] BusResult Stop(const std::string& name);
		[[nodiscard]] BusResult Restart(const std::string& name);

		// the loaded units and the unit files (name to state) as one list, sorted by name; mainPids by unit name
		[[nodiscard]] static std::vector<ServiceInfo> Merge(const std::vector<UnitEntry>& units,
			const std::unordered_map<std::string, std::string>& unitFiles, const std::unordered_map<std::string, uint32_t>& mainPids);
		// a unit file's path to its name ("/usr/lib/systemd/system/ssh.service" to "ssh.service")
		[[nodiscard]] static std::string UnitNameOfPath(const std::string& path);

	private:
		BusResult CallJob(const char* method, const std::string& name);

		sd_bus* m_Bus{ nullptr };
		int m_ConnectError{};
		std::unordered_map<std::string, std::string> m_UnitFiles;	// name to state
		bool m_HaveUnitFiles{ false };
	};
}
