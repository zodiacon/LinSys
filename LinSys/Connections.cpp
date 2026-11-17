#include "Connections.h"
#include "ProcFs.h"

#include <arpa/inet.h>
#include <charconv>
#include <cstring>
#include <dirent.h>

namespace LinSys {
	namespace {
		std::string_view NextField(std::string_view& text) {
			auto start = text.find_first_not_of(" \t");
			if (start == std::string_view::npos) {
				text = {};
				return {};
			}
			text.remove_prefix(start);
			auto end = text.find_first_of(" \t");
			auto field = text.substr(0, end);
			text.remove_prefix(end == std::string_view::npos ? text.size() : end);
			return field;
		}

		template<typename T>
		bool Number(std::string_view text, T& value, int base = 10) {
			auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value, base);
			return ec == std::errc() && ptr == text.data() + text.size();
		}

		// "0100007F:0035": the address (8 or 32 hex digits, native-order words) and the port (in hex)
		bool ParseEndpoint(std::string_view text, std::array<uint8_t, 16>& address, uint16_t& port) {
			auto colon = text.find(':');
			if (colon == std::string_view::npos)
				return false;
			auto hex = text.substr(0, colon);
			if (hex.size() != 8 && hex.size() != 32)
				return false;
			address.fill(0);
			for (size_t word = 0; word < hex.size() / 8; word++) {
				uint32_t value;
				if (!Number(hex.substr(word * 8, 8), value, 16))
					return false;
				std::memcpy(address.data() + word * 4, &value, 4);
			}
			return Number(text.substr(colon + 1), port, 16);
		}

		std::string AddressText(const std::array<uint8_t, 16>& address, bool ipv6) {
			char text[INET6_ADDRSTRLEN];
			if (!::inet_ntop(ipv6 ? AF_INET6 : AF_INET, address.data(), text, sizeof(text)))
				return {};
			return text;
		}
	}

	bool Connection::IsListening() const {
		if (IsTcp())
			return State == TcpState::Listen;
		return RemotePort == 0;
	}

	std::string Connection::LocalAddressText() const {
		return AddressText(LocalAddress, IsIPv6());
	}

	std::string Connection::RemoteAddressText() const {
		return AddressText(RemoteAddress, IsIPv6());
	}

	std::vector<Connection> Connections::Parse(std::string_view text, Protocol protocol) {
		std::vector<Connection> connections;
		while (!text.empty()) {
			auto end = text.find('\n');
			auto line = text.substr(0, end);
			text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);

			// "sl: local remote st tx:rx tr:when retrnsmt uid timeout inode ..."; the header's first field is "sl"
			auto slot = NextField(line);
			if (slot.empty() || slot.back() != ':')
				continue;
			Connection c;
			c.Proto = protocol;
			auto local = NextField(line);
			auto remote = NextField(line);
			auto state = NextField(line);
			auto queues = NextField(line);
			NextField(line);	// timer
			NextField(line);	// retransmits
			auto uid = NextField(line);
			NextField(line);	// timeout
			auto inode = NextField(line);

			uint8_t stateValue;
			auto colon = queues.find(':');
			if (!ParseEndpoint(local, c.LocalAddress, c.LocalPort) || !ParseEndpoint(remote, c.RemoteAddress, c.RemotePort)
				|| !Number(state, stateValue, 16) || colon == std::string_view::npos
				|| !Number(queues.substr(0, colon), c.SendQueue, 16) || !Number(queues.substr(colon + 1), c.ReceiveQueue, 16)
				|| !Number(uid, c.Uid) || !Number(inode, c.Inode))
				continue;
			c.State = static_cast<TcpState>(stateValue);
			connections.push_back(c);
		}
		return connections;
	}

	std::vector<Connection> Connections::Get(const std::string& procRoot) {
		std::vector<Connection> connections;
		struct Table {
			const char* Name;
			Protocol Proto;
		} tables[] = { { "tcp", Protocol::Tcp }, { "tcp6", Protocol::Tcp6 }, { "udp", Protocol::Udp }, { "udp6", Protocol::Udp6 } };
		for (auto& table : tables) {
			auto text = ProcFs::ReadFile(procRoot + "/net/" + table.Name);
			if (!text)
				continue;
			auto parsed = Parse(*text, table.Proto);
			connections.insert(connections.end(), parsed.begin(), parsed.end());
		}
		return connections;
	}

	const char* Connections::ProtocolName(Protocol protocol) {
		switch (protocol) {
			case Protocol::Tcp: return "TCP";
			case Protocol::Tcp6: return "TCPv6";
			case Protocol::Udp: return "UDP";
			case Protocol::Udp6: return "UDPv6";
		}
		return "";
	}

	const char* Connections::StateName(TcpState state) {
		switch (state) {
			case TcpState::Established: return "Established";
			case TcpState::SynSent: return "SYN sent";
			case TcpState::SynReceived: return "SYN received";
			case TcpState::FinWait1: return "FIN wait 1";
			case TcpState::FinWait2: return "FIN wait 2";
			case TcpState::TimeWait: return "Time wait";
			case TcpState::Close: return "Closed";
			case TcpState::CloseWait: return "Close wait";
			case TcpState::LastAck: return "Last ACK";
			case TcpState::Listen: return "Listen";
			case TcpState::Closing: return "Closing";
			case TcpState::NewSynReceived: return "New SYN received";
		}
		return "";
	}

	uint64_t SocketOwners::SocketInode(std::string_view link) {
		if (!link.starts_with("socket:[") || !link.ends_with(']'))
			return 0;
		uint64_t inode;
		return Number(link.substr(8, link.size() - 9), inode) ? inode : 0;
	}

	void SocketOwners::Scan() {
		m_Owners.clear();
		auto dir = ::opendir(m_ProcRoot.c_str());
		if (!dir)
			return;
		while (auto entry = ::readdir(dir)) {
			uint32_t pid;
			if (!Number(std::string_view(entry->d_name), pid) || pid == 0)
				continue;
			auto fdPath = m_ProcRoot + "/" + entry->d_name + "/fd";
			auto fds = ::opendir(fdPath.c_str());
			if (!fds)
				continue;	// another user's process, without root
			while (auto fd = ::readdir(fds)) {
				if (fd->d_name[0] == '.')
					continue;
				auto link = ProcFs::ReadLink(fdPath + "/" + fd->d_name);
				if (!link)
					continue;
				// a socket shared by processes (a server's forked workers): the first one found
				if (auto inode = SocketInode(*link))
					m_Owners.emplace(inode, pid);
			}
			::closedir(fds);
		}
		::closedir(dir);
	}

	bool SocketOwners::Resolve(std::vector<Connection>& connections) {
		// a socket not seen before: its owner is found only by reading the fd links again
		bool scan = false;
		for (auto& c : connections)
			if (c.Inode && !m_Owners.contains(c.Inode) && !m_Unknown.contains(c.Inode)) {
				scan = true;
				break;
			}
		if (scan)
			Scan();

		std::unordered_set<uint64_t> unknown;
		for (auto& c : connections) {
			c.Pid = 0;
			if (!c.Inode)
				continue;
			if (auto it = m_Owners.find(c.Inode); it != m_Owners.end())
				c.Pid = it->second;
			else
				unknown.insert(c.Inode);
		}
		// only the sockets still there are remembered as unknown
		m_Unknown = std::move(unknown);
		return scan;
	}
}
