#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <string>
#include <functional>
#include <vector>
#include <list>
//#include <map>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <queue>
#include "socket.h"

#ifdef _DEBUG
std::mutex m_global;
#include <iostream>
using namespace std;
#endif

#pragma comment(lib, "Ws2_32.lib")

using namespace std::chrono_literals;

sockets::asio::socket::socket(io_service & io_svc, SOCKET sSocket):io_svc(io_svc), sSocket(sSocket)
{
	this->sSocket = sSocket;
	socket_state = sSocket == INVALID_SOCKET ? socket_states::disconnected : socket_states::connected;
}

sockets::asio::socket::socket(socket && other) : io_svc(other.io_svc)
{
	this->sSocket = other.sSocket;
	this->socket_state = other.socket_state;
	other.sSocket = INVALID_SOCKET;
}

sockets::asio::socket::socket(io_service & io_svc, socket && other):io_svc(io_svc)
{
	this->sSocket = other.sSocket;
	this->socket_state = other.socket_state;
	other.sSocket = INVALID_SOCKET;
}

sockets::asio::socket::~socket()
{
	this->shutdown();
	this->close();
}

void sockets::asio::socket::shutdown()
{
	if (sSocket != INVALID_SOCKET)
		::shutdown(sSocket, SD_BOTH);
}

void sockets::asio::socket::close()
{
	if (sSocket != INVALID_SOCKET) {
		::closesocket(sSocket);
		sSocket = INVALID_SOCKET;
	}
}

void sockets::asio::socket::on_msg(sMsg * msg)
{
}

bool sockets::asio::socket::write_msg(USHORT id, const std::vector<BYTE>& msg, std::function<void(bool bCompleted)> cb)
{
	if (!is_state_connected())
		return false;
	sNetworkMsgWrite network_msg;
	network_msg.pos = 0;
	network_msg.msg.resize(sizeof(sRawNetworkMsgHdr) + msg.size()); 
	auto msg_hdr = reinterpret_cast<sRawNetworkMsgHdr*>(network_msg.msg.data());
	msg_hdr->wMsgID = id;
	msg_hdr->wMsgLen = msg.size();
	memcpy(msg_hdr + 1, msg.data(), msg.size());
	network_msg.cb = cb;
	std::lock_guard<std::mutex> lock(io.o.m);
	if (!is_state_connected())
		return false;
	io.o.msgs.push_back(std::move(network_msg));
	return true;
}


void sockets::asio::socket::process_read(uint8_t* data, int32_t len)
{ 
	int32_t hdr_bytes_left = static_cast<int32_t>(io.i.msg.size()) > sizeof(sRawNetworkMsgHdr) ? 0 : (sizeof(sRawNetworkMsgHdr) - static_cast<int32_t>(io.i.msg.size()));
	if (hdr_bytes_left) {
		auto reading = hdr_bytes_left >= len ? len : hdr_bytes_left;
		auto current_hdr_len = io.i.msg.size();
		io.i.msg.resize(io.i.msg.size() + reading);
		memcpy(&io.i.msg[current_hdr_len], data, reading);
		data += reading;
		len -= reading;
		if (io.i.msg.size() == sizeof(sRawNetworkMsgHdr))
			this->check_msg_completion();
	}
	else {
		auto pMsg = reinterpret_cast<sRawNetworkMsgHdr*>(io.i.msg.data());
		if (io.i.msg.capacity() < pMsg->wMsgLen + sizeof(sRawNetworkMsgHdr)) {
			io.i.msg.reserve(pMsg->wMsgLen + sizeof(sRawNetworkMsgHdr));
			pMsg = reinterpret_cast<sRawNetworkMsgHdr*>(io.i.msg.data());
		}
		int32_t msg_bytes_left = int32_t(pMsg->wMsgLen + sizeof(sRawNetworkMsgHdr)) - static_cast<int32_t>(io.i.msg.size());
		auto to_read = len < msg_bytes_left ? len : msg_bytes_left;
		auto bytes_read = io.i.msg.size();
		io.i.msg.resize(bytes_read + to_read);
		memcpy(&io.i.msg[bytes_read], data, to_read);
		data += to_read;
		len -= to_read;
		if (msg_bytes_left == to_read)
			this->check_msg_completion();
	}

	if (len > 0)
		return process_read(data, len);
}

void sockets::asio::socket::process_internal_msg(sMsg * msg)
{
	switch (msg->wMsgID) {
	case internal_msg_types::id_disconnect:
	{
		this->disconnect(true);
	}
	break;
	case internal_msg_types::id_ping:
	{

	}
	break;
	case internal_msg_types::id_pong:
	{

	}
	break;
	}
}

void sockets::asio::socket::check_msg_completion()
{
	if (io.i.msg.size() < sizeof(sRawNetworkMsgHdr))
		return;
	auto pMsg = reinterpret_cast<sRawNetworkMsgHdr*>(io.i.msg.data());
	if (pMsg->wMsgLen == 0 || pMsg->wMsgLen + sizeof(sRawNetworkMsgHdr) == io.i.msg.size()) {
		if (pMsg->wMsgID <= id_internal_msg_max) {
			this->process_internal_msg(static_cast<sMsg*>(pMsg));
			io.i.msg.clear();
		}
		else
			io_svc.on_network_msg(shared_from_this(), std::move(io.i.msg));
	}
}

size_t sockets::asio::socket::create_queue_ticket()
{
	auto r = queue_tickets + 1;
	while (InterlockedCompareExchange(&queue_tickets, r, r - 1) != r - 1)
		r = queue_tickets + 1;
	return r;
}

void sockets::asio::socket::disconnect(bool graceful)
{
	if (disconnected() || (graceful && disconnecting()))
		return;
	if (graceful) {
		grace_period_start = std::chrono::steady_clock::now();
		socket_state = socket_states::disconnecting;
	}
	else
		socket_state = socket_states::disconnected;
}

void sockets::asio::socket::on_bytes_read(uint8_t * data, int32_t len)
{
#ifdef _DEBUG
	std::lock_guard<std::mutex> global_lck(m_global);
	cout << "socket: " << std::hex << sSocket << " received " << std::dec << len << " bytes" << endl;
#endif
	if (len <= 0)
		disconnect(false);
	else
		process_read(data, len);
}

void sockets::asio::socket::on_bytes_written(sNetworkMsgWrite * msg, int written)
{
#ifdef _DEBUG
	std::lock_guard<std::mutex> global_lck(m_global);
	cout << "socket: " << std::hex << sSocket << " sent " << std::dec << written << " bytes" << endl;
#endif
	if (written <= 0) {
		disconnect(false);
		if (msg->cb)
			msg->cb(false);
	}
	if (msg->pos == msg->msg.size())
		if (msg->cb)
			msg->cb(true);
}


sockets::asio::io_service::io_service(int nWorkers)
{
	bQuit = bQuitted = false;
	for (int i = 0; i < nWorkers; i++)
		workers.push_back(std::thread(&io_service::worker, this));
	disconnect_grace_period = 60s;
}

sockets::asio::io_service::~io_service()
{
	this->quit();
	this->completed_io.cv.notify_all();
	for (auto& worker : workers)
		if (worker.joinable())
			worker.join();
	sockets.clear();
	workers.clear();
}

void sockets::asio::io_service::poll(int timeout)
{
	std::unique_lock<std::recursive_mutex> lock(m_sockets);

	if (sockets.size() == 0) {
		lock.unlock();
		std::this_thread::sleep_for(20ms);
		return;
	}

	std::unordered_map<SOCKET, std::shared_ptr<socket>> polling;
	chk.pool.reserve(sockets.size());
	chk.pool.clear();
	for (auto s = sockets.begin(); s != sockets.end();) {
		auto &socket = (*s);
		if (socket->disconnected()) {
			s = sockets.erase(s);
			continue;
		}
		//check timeout, etc

		WSAPOLLFD fd;
		fd.fd = socket->sSocket;
		fd.events = fd.revents = 0;
		//check if can read, then:
		fd.events |= POLLRDNORM;

		if (socket->io.o.msgs.size())
			fd.events |= POLLWRNORM;

		chk.pool.push_back(fd);
		polling[socket->sSocket] = socket;
		
		++s;
	}

	lock.unlock();

	if (chk.pool.size() == 0)
		return;

	int ret;
	if (SOCKET_ERROR == (ret = WSAPoll(chk.pool.data(), static_cast<ULONG>(chk.pool.size()), timeout))) {
#ifdef _DEBUG
		std::lock_guard<std::mutex> global_lck(m_global);
		std::cout << "WSAPoll failed!" << std::endl;
#endif
	}

	if (ret > 0) {
		for (auto& fd : chk.pool) {
			if (fd.revents & POLLRDNORM)
				this->process_io(io_rd, polling[fd.fd]);
			if (fd.revents & POLLWRNORM)
				this->process_io(io_wr, polling[fd.fd]);
			if ((fd.revents & POLLERR) || (fd.revents & POLLHUP) || (fd.revents & POLLNVAL)) {
#ifdef _DEBUG
				std::lock_guard<std::mutex> global_lck(m_global);
				printf("socket died: 0x%x\r\n", (int)fd.fd);
#endif
				polling[fd.fd]->disconnect(false);
			}
		}
	}

}

void sockets::asio::io_service::run()
{
	bQuitted = false;
	while (!bQuit)
		poll();
	if (bGracefulQuit) {
		
		//...
	}
	bQuitted = true;
}

void sockets::asio::io_service::quit(bool graceful)
{
	if (bQuit)
		return;
	bGracefulQuit = graceful;
	bQuit = true;
}

void sockets::asio::io_service::wait()
{
	while (!bQuitted)
		std::this_thread::sleep_for(25ms);
}

void sockets::asio::io_service::enumerate_sockets(std::function<void(std::shared_ptr<socket>&)> f)
{
	std::unique_lock<std::recursive_mutex> lock(m_sockets);
	for (auto& socket : sockets)
		f(socket);
}

void sockets::asio::io_service::push_socket(std::shared_ptr<socket> socket)
{
	std::lock_guard<std::recursive_mutex> lock(m_sockets);
	sockets.push_back(std::move(socket));
}

void sockets::asio::io_service::worker()
{
	while (!bQuitted) {
		std::unique_lock<std::mutex> lock(completed_io.m_queue_lock);
		completed_io.cv.wait(lock, [this]() -> bool { return bQuitted || !completed_io.msgs.empty(); });
		if (completed_io.msgs.size()) {
			auto pair = std::move(completed_io.msgs.front());
			completed_io.msgs.pop();
			auto ptr = pair.first.lock();
			if (!ptr) //|| ptr->disconnected()
				continue;
			auto ticket = ptr->create_queue_ticket();
			lock.unlock();
			while (ticket != ptr->current_queue_ticket)
				std::this_thread::sleep_for(1ms);
			ptr->on_msg(reinterpret_cast<sMsg*>(pair.second.data()));
			InterlockedIncrement(&ptr->current_queue_ticket);
			//std::this_thread::sleep_for(25ms);
		}
	}
}

void sockets::asio::io_service::on_network_msg(std::weak_ptr<socket> socket, std::vector<BYTE>&& msg)
{
	{
		std::lock_guard<std::mutex> completed_io_lock(this->completed_io.m_queue_lock);
		this->completed_io.msgs.push(std::make_pair(socket, std::move(msg)));
	}
	this->completed_io.cv.notify_one();
}

void sockets::asio::io_service::process_io(io_types io_type, std::shared_ptr<socket> socket)
{
	char buf[65535];
	switch (io_type) {
	case io_types::io_rd:
	{
		int i = ::recv(socket->sSocket, buf, sizeof(buf), 0);
		socket->on_bytes_read(reinterpret_cast<uint8_t*>(buf), i);
	}
	break;
	case io_types::io_wr:
	{
		std::lock_guard<std::mutex> lock(socket->io.o.m);
		auto msg = socket->io.o.msgs.begin();
		if (msg != socket->io.o.msgs.end()) {
			auto& cMsg = (*msg);
			auto left = cMsg.msg.size() - cMsg.pos;
			int i = ::send(socket->sSocket, reinterpret_cast<const char*>(&cMsg.msg.data()[cMsg.pos]), left, 0);
			if (i > 0)
				cMsg.pos += i;
			socket->on_bytes_written(&cMsg, i);
			if (cMsg.msg.size() == cMsg.pos)
				socket->io.o.msgs.erase(msg);
		}
	}
	break;
	}
}

namespace sockets {
	namespace asio {

		listener::listener()
		{
			bStop = true;
			bDisconnected = true;
			iSockets = NULL;
			wPort = NULL;
			func = nullptr;
		}

		listener::~listener()
		{
			this->close();
		}

		void listener::close(bool from_inside_lthread)
		{
			bDisconnected = bStop = true;
			if (!from_inside_lthread) {
				if (t.joinable())
					t.join();
			}
			for (int i = 0; i < iSockets; i++)
				::closesocket(sSockets[i]);
			iSockets = NULL;
			wPort = NULL;
			for (int i = 0; i < 2; i++)
				IPStatus[i] = false;
			func = nullptr;
		}

		bool listener::listen(WORD wPort, listener_flags flags)
		{
			this->close();
			addrinfo *result = nullptr, *ptr = nullptr, hints;
			ZeroMemory(&hints, sizeof(hints));
			if ((flags & IPv4) > 0 && (flags & IPv6) > 0)
				hints.ai_family = AF_UNSPEC;
			else if (flags & IPv4)
				hints.ai_family = AF_INET;
			else if (flags & IPv6)
				hints.ai_family = AF_INET6;
			else
				hints.ai_family = AF_UNSPEC;

			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;
			hints.ai_flags = AI_PASSIVE;
			char port[6];
			sprintf_s(port, "%d", wPort);
			PCSTR pNodeName; //nullptr works but we'll play by the books
			if (flags & LOOPBACK)
				pNodeName = "loopback";
			else
				pNodeName = "";
			if (getaddrinfo(pNodeName, port, &hints, &result) == 0) {
				for (ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
					if (ptr->ai_protocol != IPPROTO_TCP)
						continue;
					if (iSockets >= FD_SETSIZE)
						break;

					sSockets[iSockets] = ::socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
					if (sSockets[iSockets] == INVALID_SOCKET)
						continue;
					/*if (flags & listener_flags::LOOPBACK) {
						SOCKADDR_STORAGE addr{};
						addr.ss_family = ptr->ai_family;
						switch (ptr->ai_family) {
						case AF_INET:
							InetPton(AF_INET, TEXT("127.0.0.1"), &addr); //INADDR_LOOPBACK
							break;
						case AF_INET6:
							InetPton(AF_INET6, TEXT("::1"), &addr); //&reinterpret_cast<SOCKADDR_IN6*>(ptr->ai_addr)->sin6_addr
							break;
						}

						if (::bind(sSockets[iSockets], (const sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
							::closesocket(sSockets[iSockets]);
							continue;
						}
					}
					else {
						*/
					if (::bind(sSockets[iSockets], ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == SOCKET_ERROR) {
						::closesocket(sSockets[iSockets]);
						continue;
					}

					if (::listen(sSockets[iSockets], SOMAXCONN) == SOCKET_ERROR) {
						::closesocket(sSockets[iSockets]);
						continue;
					}
					switch (ptr->ai_family) {
					case AF_INET:
						IPStatus[ipv4] = true;
						break;
					case AF_INET6:
						IPStatus[ipv6] = true;
						break;
					}
					this->wPort = wPort;
					iSockets++;
					bStop = bDisconnected = false;
				}
				freeaddrinfo(result);
				return !(bDisconnected = iSockets == NULL);
			}
			else
				return false;
		}

		void listener::accept(std::function<void(SOCKET)> f)
		{
			if (bStop || func)
				return;
			func = f;
			t = std::thread(&listener::run, this);
		}

		void listener::run()
		{
			TIMEVAL t;
			t.tv_sec = 0;
			t.tv_usec = 20000;

			while (!bStop && !disconnected())
			{
				FD_SET set;
				FD_ZERO(&set);
				for (int i = 0; i < iSockets; i++)
					FD_SET(sSockets[i], &set);
				int i;
				if ((i = select(0, &set, nullptr, nullptr, &t)) > 0) {
					SOCKADDR_STORAGE discard;
					int size = sizeof(discard);
					SOCKET s = ::accept(set.fd_array[0], reinterpret_cast<LPSOCKADDR>(&discard), &size);
					if (s != INVALID_SOCKET) {
						if (func) {
							func(s);
						}
						else
							::closesocket(s);
					}
				}
				else if (i == SOCKET_ERROR) //returns 0 if timed out, -1 means an error occured.
					this->close(true);
				//Sleep(100);
			}
		}


		connector::connector(std::function<void(SOCKET)> default_func)
		{
			this->default_func = default_func;
			bStop = false;
			t = std::thread(&connector::run, this);
		}

		connector::~connector()
		{
			this->cancel();
		}

		void connector::wait()
		{
			while (size())
				std::this_thread::sleep_for(1ms);
		}

		void connector::cancel()
		{
			bStop = true;
			if (t.joinable())
				t.join();
			for (auto& sock : s)
				::closesocket(sock.first.first);
			s.clear();
		}

		void connector::run()
		{
			while (!bStop) {
				this->check();
				Sleep(50);
			}
		}

		bool connector::connect(const std::string& host, WORD port, const std::chrono::milliseconds& duration)
		{
			return this->connect(host, port, duration, nullptr);
		}

		bool connector::connect(const std::string & host, WORD port, const std::chrono::milliseconds& expiration, std::function<void(SOCKET)> cb)
		{
			char sPort[6];
			sprintf_s(sPort, "%d", port);
			ADDRINFO hints = {}, *AI;
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;
			if (getaddrinfo(host.c_str(), sPort, &hints, &AI) != 0) {
				if (cb)
					cb(INVALID_SOCKET);
				else
					if (default_func)
						default_func(INVALID_SOCKET);
				return false;
			}
			SOCKET sSocket = ::socket(AI->ai_addr->sa_family, SOCK_STREAM, IPPROTO_TCP);
			u_long mode = 1;  //enable non-blocking mode on the socket
			ioctlsocket(sSocket, FIONBIO, &mode);
			bool bSuccess = false;
			if (::connect(sSocket, AI->ai_addr, static_cast<int>(AI->ai_addrlen)) == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
				std::lock_guard<std::mutex> lock(m);
				s.push_back(std::make_pair(std::make_pair(sSocket, cb), std::chrono::steady_clock::now() + expiration)); //std::pair<std::pair<SOCKET, std::function<void(SOCKET)>>, std::chrono::steady_clock::time_point>
				bSuccess = true;
			}
			else {
				::closesocket(sSocket);
				if (cb)
					cb(INVALID_SOCKET);
				else
					if (default_func)
						default_func(INVALID_SOCKET);
			}
			freeaddrinfo(AI);
			return bSuccess;
		}

		size_t connector::size() const
		{
			std::lock_guard<std::mutex> lock(m);
			return s.size();
		}

		bool connector::check(FD_SET& s)
		{
			if (s.fd_count == NULL)
				return false;
			TIMEVAL t;
			t.tv_sec = 0;
			t.tv_usec = 1000;
			return ::select(0, nullptr, &s, nullptr, &t) > 0;
		}

		void connector::process(std::list<SOCKET>& valid, FD_SET& w)
		{
			for (u_int i = 0; i < w.fd_count; i++) {
				auto f = std::find_if(s.begin(), s.end(), [&](const std::pair<std::pair<SOCKET, std::function<void(SOCKET)>>, std::chrono::steady_clock::time_point>& value) { //const auto& value
					return value.first.first == w.fd_array[i];
					});
				if (f == s.end()) { //this should never happen
					::closesocket(w.fd_array[i]);
					continue;
				}

				u_long mode = 0;
				ioctlsocket(w.fd_array[i], FIONBIO, &mode); //disable non-blocking mode

				if (f->first.second)
					f->first.second(w.fd_array[i]);
				else {
					if (default_func)
						default_func(w.fd_array[i]);
					else
						::closesocket(w.fd_array[i]);
				}

				valid.push_back(w.fd_array[i]);
			}
			FD_ZERO(&w);
		}

		void connector::check()
		{
			std::lock_guard<std::mutex> lock(m);
			for (auto it = s.begin(); it != s.end();) {
				if (std::chrono::steady_clock::now() > it->second) {
					::closesocket(it->first.first);
					if (it->first.second)
						it->first.second(INVALID_SOCKET);
					else
						if (default_func)
							default_func(INVALID_SOCKET);
					it = s.erase(it);
				}
				else
					++it;
			}
			FD_SET w;
			FD_ZERO(&w);
			std::list<SOCKET> valid;
			for (auto& sock : s) {
				FD_SET(sock.first.first, &w);
				if (w.fd_count == FD_SETSIZE)
					if (check(w))
						process(valid, w);
			}

			if (check(w))
				process(valid, w);

			s.remove_if([&](const auto& p) -> bool {
				return std::find(valid.begin(), valid.end(), p.first.first) != valid.end();
				});

		}


	}
}