#include "TestFramework.h"
#include "FakeProc.h"
#include <LinSys/Connections.h>

#include <arpa/inet.h>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace LinSys;
namespace fs = std::filesystem;

// lines as WSL's kernel writes them (x86: the words little-endian)
TEST(Connections_ParseTcp) {
	auto connections = Connections::Parse(
		"  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
		"   0: 3600007F:0035 00000000:0000 0A 00000000:00000000 00:00000000 00000000   989        0 15406 1 0000000000000000 100 0 0 10 5\n"
		"   2: FEFFFF0A:0035 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 8208 1 0000000000000000 100 0 0 10 0\n"
		"   3: 0F02000A:C350 2E0B5B8C:01BB 01 0000002A:00000100 02:000000AF 00000000  1000        0 77001 2 0000000000000000 20 4 30 10 -1\n"
		"   4: 0F02000A:C351 2E0B5B8C:01BB 06 00000000:00000000 03:00000F4F 00000000     0        0 0 3 0000000000000000\n"
		"   5: garbage\n", Protocol::Tcp);
	CHECK_EQ(connections.size(), 4u);
	if (connections.size() != 4)
		return;
	CHECK_EQ(connections[0].LocalAddressText(), std::string("127.0.0.54"));
	CHECK_EQ(connections[0].LocalPort, 53);
	CHECK(connections[0].State == TcpState::Listen);
	CHECK(connections[0].IsListening());
	CHECK_EQ(connections[0].Uid, 989u);
	CHECK_EQ(connections[0].Inode, 15406ull);
	CHECK_EQ(connections[1].LocalAddressText(), std::string("10.255.255.254"));

	auto& https = connections[2];
	CHECK_EQ(https.LocalAddressText(), std::string("10.0.2.15"));
	CHECK_EQ(https.LocalPort, 50000);
	CHECK_EQ(https.RemoteAddressText(), std::string("140.91.11.46"));
	CHECK_EQ(https.RemotePort, 443);
	CHECK(https.State == TcpState::Established);
	CHECK(!https.IsListening());
	CHECK_EQ(https.SendQueue, 42u);
	CHECK_EQ(https.ReceiveQueue, 256u);
	CHECK(connections[3].State == TcpState::TimeWait);
	CHECK_EQ(connections[3].Inode, 0ull);
}

TEST(Connections_ParseUdp6) {
	auto connections = Connections::Parse(
		"  sl  local_address                         remote_address                        st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode ref pointer drops\n"
		"15486: 00000000000000000000000001000000:0143 00000000000000000000000000000000:0000 07 00000000:00000000 00:00000000 00000000     0        0 5695 2 0000000000000000 0\n"
		"15487: 0000000000000000FFFF00000F02000A:1F90 00000000000000000000000000000000:0000 07 00000000:00000000 00:00000000 00000000  1000        0 5696 2 0000000000000000 0\n",
		Protocol::Udp6);
	CHECK_EQ(connections.size(), 2u);
	if (connections.size() != 2)
		return;
	CHECK_EQ(connections[0].LocalAddressText(), std::string("::1"));
	CHECK_EQ(connections[0].LocalPort, 323);
	CHECK_EQ(connections[0].RemoteAddressText(), std::string("::"));
	CHECK(connections[0].IsListening());	// UDP with no peer
	CHECK_EQ(connections[1].LocalAddressText(), std::string("::ffff:10.0.2.15"));
	CHECK_EQ(std::string(Connections::ProtocolName(connections[1].Proto)), std::string("UDPv6"));
}

TEST(SocketOwners_SocketInode) {
	CHECK_EQ(SocketOwners::SocketInode("socket:[15406]"), 15406ull);
	CHECK_EQ(SocketOwners::SocketInode("pipe:[15406]"), 0ull);
	CHECK_EQ(SocketOwners::SocketInode("socket:[x]"), 0ull);
	CHECK_EQ(SocketOwners::SocketInode("/home/user/socket:[1]"), 0ull);
}

// owners from the fd links; read again only for a socket not seen before
TEST(SocketOwners_Resolve) {
	Tests::FakeProc proc;
	proc.SetProcess(100, "server", 1, 0, 0, 10);
	proc.SetProcess(200, "client", 1, 0, 0, 20);
	fs::create_directories(proc.Root() + "/100/fd");
	fs::create_directories(proc.Root() + "/200/fd");
	fs::create_symlink("socket:[15406]", proc.Root() + "/100/fd/3");
	fs::create_symlink("pipe:[9]", proc.Root() + "/100/fd/4");
	fs::create_symlink("socket:[77001]", proc.Root() + "/200/fd/5");

	auto connection = [](uint64_t inode) {
		Connection c;
		c.Inode = inode;
		return c;
	};
	std::vector<Connection> connections{ connection(15406), connection(77001), connection(99999), connection(0) };
	SocketOwners owners(proc.Root());
	CHECK(owners.Resolve(connections));
	CHECK_EQ(connections[0].Pid, 100u);
	CHECK_EQ(connections[1].Pid, 200u);
	CHECK_EQ(connections[2].Pid, 0u);	// another user's (no link found)
	CHECK_EQ(connections[3].Pid, 0u);	// TIME_WAIT: no socket

	// nothing new (99999 already looked for): no reading
	CHECK(!owners.Resolve(connections));
	CHECK_EQ(connections[1].Pid, 200u);

	// a new socket: read again
	fs::create_symlink("socket:[88002]", proc.Root() + "/200/fd/6");
	connections.push_back(connection(88002));
	CHECK(owners.Resolve(connections));
	CHECK_EQ(connections.back().Pid, 200u);
}

// a socket this process listens on: in the table, with this process as its owner
TEST(Connections_Live) {
	int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	CHECK(fd >= 0);
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECK_EQ(::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
	CHECK_EQ(::listen(fd, 1), 0);
	socklen_t length = sizeof(address);
	::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length);
	auto port = ntohs(address.sin_port);

	auto connections = Connections::Get();
	SocketOwners owners;
	owners.Resolve(connections);
	bool found = false;
	for (auto& c : connections)
		if (c.Proto == Protocol::Tcp && c.LocalPort == port && c.State == TcpState::Listen) {
			found = true;
			CHECK_EQ(c.LocalAddressText(), std::string("127.0.0.1"));
			CHECK_EQ(c.Pid, static_cast<uint32_t>(::getpid()));
			CHECK_EQ(c.Uid, static_cast<uint32_t>(::getuid()));
		}
	CHECK(found);
	::close(fd);
}
