#include "Services.h"

#include <algorithm>
#include <cerrno>
#include <systemd/sd-bus.h>

namespace LinSys {
	namespace {
		constexpr const char* Destination = "org.freedesktop.systemd1";
		constexpr const char* ManagerPath = "/org/freedesktop/systemd1";
		constexpr const char* ManagerInterface = "org.freedesktop.systemd1.Manager";

		// sd_bus_error and sd_bus_message freed on scope exit
		struct BusError {
			sd_bus_error Value = SD_BUS_ERROR_NULL;
			~BusError() { sd_bus_error_free(&Value); }
		};
		struct BusMessage {
			sd_bus_message* Value{ nullptr };
			~BusMessage() { sd_bus_message_unref(Value); }
		};

		BusResult ResultOf(int r, const BusError& error) {
			if (r >= 0)
				return {};
			// sd-bus maps D-Bus errors to errno values (AccessDenied and InteractiveAuthorizationRequired: EACCES)
			auto code = sd_bus_error_get_errno(&error.Value);
			return { code ? code : -r, error.Value.message ? error.Value.message : "" };
		}

		bool IsRunningState(const std::string& state) {
			return state == "active" || state == "activating" || state == "deactivating" || state == "reloading";
		}
	}

	ServiceManager::ServiceManager(Bus bus) : m_BusType(bus) {
		Connect();
	}

	bool ServiceManager::Connect() {
		m_Bus = sd_bus_flush_close_unref(m_Bus);
		auto r = m_BusType == Bus::System ? sd_bus_open_system(&m_Bus) : sd_bus_open_user(&m_Bus);
		if (r >= 0) {
			// let polkit ask (through an agent, if there is one) instead of refusing outright
			sd_bus_set_allow_interactive_authorization(m_Bus, 1);
			// sd-bus authenticates lazily, on the first call; the bus daemon drops a connection that hasn't
			// authenticated within its timeout (under a minute), so the first List, when the view is shown, would
			// get ENOTCONN. The unique name waits for the handshake (Hello) to finish
			const char* unique;
			r = sd_bus_get_unique_name(m_Bus, &unique);
		}
		if (r < 0) {
			m_ConnectError = -r;
			m_Bus = sd_bus_flush_close_unref(m_Bus);
			return false;
		}
		m_ConnectError = 0;
		return true;
	}

	ServiceManager::~ServiceManager() {
		sd_bus_flush_close_unref(m_Bus);
	}

	std::string ServiceManager::UnitNameOfPath(const std::string& path) {
		auto slash = path.rfind('/');
		return slash == std::string::npos ? path : path.substr(slash + 1);
	}

	std::vector<ServiceInfo> ServiceManager::Merge(const std::vector<UnitEntry>& units,
		const std::unordered_map<std::string, std::string>& unitFiles, const std::unordered_map<std::string, uint32_t>& mainPids) {
		std::vector<ServiceInfo> services;
		services.reserve(units.size() + unitFiles.size());
		std::unordered_map<std::string, bool> loaded;
		for (auto& unit : units) {
			ServiceInfo service{ unit.Name, unit.Description, unit.LoadState, unit.ActiveState, unit.SubState, {}, 0 };
			if (auto it = unitFiles.find(unit.Name); it != unitFiles.end())
				service.UnitFileState = it->second;
			if (auto it = mainPids.find(unit.Name); it != mainPids.end())
				service.MainPid = it->second;
			loaded[unit.Name] = true;
			services.push_back(std::move(service));
		}
		for (auto& [name, state] : unitFiles) {
			if (loaded.contains(name) || name.ends_with("@.service"))
				continue;
			services.push_back({ name, {}, {}, "inactive", "dead", state, 0 });
		}
		std::sort(services.begin(), services.end(), [](auto& a, auto& b) { return a.Name < b.Name; });
		return services;
	}

	BusResult ServiceManager::List(std::vector<ServiceInfo>& services, bool refreshUnitFiles) {
		services.clear();
		if (!m_Bus)
			return { m_ConnectError, "Not connected to the system bus" };

		std::vector<UnitEntry> units;
		{
			BusError error;
			BusMessage reply;
			// all states (an empty list), services only
			auto list = [&] {
				return sd_bus_call_method(m_Bus, Destination, ManagerPath, ManagerInterface, "ListUnitsByPatterns", &error.Value,
					&reply.Value, "asas", 0, 1, "*.service");
			};
			auto r = list();
			if (r == -ENOTCONN && Reconnect(error.Value))
				r = list();
			if (r < 0)
				return ResultOf(r, error);
			r = sd_bus_message_enter_container(reply.Value, 'a', "(ssssssouso)");
			if (r < 0)
				return { -r, "Unexpected reply to ListUnitsByPatterns" };
			const char *name, *description, *load, *active, *sub, *following, *path, *jobType, *jobPath;
			uint32_t jobId;
			while ((r = sd_bus_message_read(reply.Value, "(ssssssouso)", &name, &description, &load, &active, &sub, &following,
				&path, &jobId, &jobType, &jobPath)) > 0)
				units.push_back({ name, description, load, active, sub, path });
			if (r < 0)
				return { -r, "Unexpected reply to ListUnitsByPatterns" };
		}

		if (refreshUnitFiles || !m_HaveUnitFiles) {
			BusError error;
			BusMessage reply;
			auto r = sd_bus_call_method(m_Bus, Destination, ManagerPath, ManagerInterface, "ListUnitFilesByPatterns", &error.Value,
				&reply.Value, "asas", 0, 1, "*.service");
			if (r >= 0 && sd_bus_message_enter_container(reply.Value, 'a', "(ss)") >= 0) {
				m_UnitFiles.clear();
				const char *path, *state;
				while (sd_bus_message_read(reply.Value, "(ss)", &path, &state) > 0)
					m_UnitFiles[UnitNameOfPath(path)] = state;
				m_HaveUnitFiles = true;
			}
			// without the unit files, the loaded units are still worth showing
		}

		// the running ones' main processes: a property each
		std::unordered_map<std::string, uint32_t> mainPids;
		for (auto& unit : units) {
			if (!IsRunningState(unit.ActiveState))
				continue;
			BusError error;
			uint32_t pid = 0;
			if (sd_bus_get_property_trivial(m_Bus, Destination, unit.ObjectPath.c_str(), "org.freedesktop.systemd1.Service",
				"MainPID", &error.Value, 'u', &pid) >= 0 && pid)
				mainPids[unit.Name] = pid;
		}

		services = Merge(units, m_UnitFiles, mainPids);
		return {};
	}

	BusResult ServiceManager::CallJob(const char* method, const std::string& name) {
		if (!m_Bus)
			return { m_ConnectError, "Not connected to the system bus" };
		BusError error;
		BusMessage reply;
		auto call = [&] {
			return sd_bus_call_method(m_Bus, Destination, ManagerPath, ManagerInterface, method, &error.Value, &reply.Value,
				"ss", name.c_str(), "replace");
		};
		auto r = call();
		if (r == -ENOTCONN && Reconnect(error.Value))
			r = call();
		// what's installed may have changed (a unit file loaded by starting it)
		m_HaveUnitFiles = false;
		return ResultOf(r, error);
	}

	bool ServiceManager::Reconnect(sd_bus_error& error) {
		// ENOTCONN: the connection was closed (the bus daemon restarted) before the call was sent, so it's safe to send
		// again on a new one
		sd_bus_error_free(&error);
		return Connect();
	}

	BusResult ServiceManager::Start(const std::string& name) {
		return CallJob("StartUnit", name);
	}

	BusResult ServiceManager::Stop(const std::string& name) {
		return CallJob("StopUnit", name);
	}

	BusResult ServiceManager::Restart(const std::string& name) {
		return CallJob("RestartUnit", name);
	}
}
