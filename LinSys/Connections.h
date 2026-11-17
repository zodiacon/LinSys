#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace LinSys {
	enum class Protocol : uint8_t { Tcp, Tcp6, Udp, Udp6 };

	// the kernel's TCP states (include/net/tcp_states.h); UDP sockets show Close (unconnected) or Established
	enum class TcpState : uint8_t {
		Established = 1, SynSent, SynReceived, FinWait1, FinWait2, TimeWait, Close, CloseWait, LastAck, Listen, Closing,
		NewSynReceived
	};

	// a socket of /proc/net/tcp, tcp6, udp or udp6
	struct Connection {
		Protocol Proto{};
		std::array<uint8_t, 16> LocalAddress{}, RemoteAddress{};	// network order; IPv4 in the first 4 bytes
		uint16_t LocalPort{}, RemotePort{};
		TcpState State{};
		uint32_t SendQueue{}, ReceiveQueue{};	// bytes
		uint32_t Uid{};
		uint64_t Inode{};						// 0 for a socket no process has (TIME_WAIT)
		uint32_t Pid{};							// the owner (SocketOwners), 0 if unknown

		[[nodiscard]] bool IsIPv6() const { return Proto == Protocol::Tcp6 || Proto == Protocol::Udp6; }
		[[nodiscard]] bool IsTcp() const { return Proto == Protocol::Tcp || Proto == Protocol::Tcp6; }
		// waiting for connections: TCP's Listen, or UDP bound to no peer (remote port 0)
		[[nodiscard]] bool IsListening() const;
		// "127.0.0.1", "::1", "::ffff:10.0.0.1" (inet_ntop)
		[[nodiscard]] std::string LocalAddressText() const;
		[[nodiscard]] std::string RemoteAddressText() const;
	};

	struct Connections final {
		// a table's lines (the header skipped); the addresses are native-order 32-bit words in hex, as the kernel
		// writes them, so their bytes are copied as they are
		[[nodiscard]] static std::vector<Connection> Parse(std::string_view text, Protocol protocol);
		// the four tables of this process's network namespace (a table that can't be read, as tcp6 without IPv6, is left
		// out), owners not resolved
		[[nodiscard]] static std::vector<Connection> Get(const std::string& procRoot = "/proc");
		[[nodiscard]] static const char* ProtocolName(Protocol protocol);	// "TCP", "TCPv6"...
		[[nodiscard]] static const char* StateName(TcpState state);		// "Established", "Listen"...
	};

	// Which process has each socket: the "socket:[inode]" links in /proc/[pid]/fd. Reading every process's fd links is
	// what costs, so it's done only when a socket appears whose owner isn't known yet; a socket not found then
	// (another user's process, without root) isn't looked for again. Keep one and call Resolve on each list
	class SocketOwners {
	public:
		explicit SocketOwners(std::string procRoot = "/proc") : m_ProcRoot(std::move(procRoot)) {}

		// sets each connection's Pid; true if the fd links were read this time
		bool Resolve(std::vector<Connection>& connections);

		// "socket:[12345]" to 12345; 0 for anything else
		[[nodiscard]] static uint64_t SocketInode(std::string_view link);

	private:
		void Scan();

		std::string m_ProcRoot;
		std::unordered_map<uint64_t, uint32_t> m_Owners;	// inode to pid
		std::unordered_set<uint64_t> m_Unknown;				// not found in the last scan
	};
}
