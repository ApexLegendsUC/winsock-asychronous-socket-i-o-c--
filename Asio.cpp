#include <iostream>
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

using namespace std;

class MyClientSocket :public sockets::asio::socket {
public:
	using sockets::asio::socket::socket;
	void on_msg(sockets::asio::sMsg* msg)
	{
		switch (msg->wMsgID) {
		case 1338:
		{
			cout << "msg=" << std::string((const char*)msg->data, msg->wMsgLen) << endl;
		}
		break;
		}

	};
};

void fake_client()
{
	sockets::asio::io_service io;

	sockets::asio::connector connector;

	connector.connect("::1", 1338, 20s, [&](SOCKET sSocket) {
		if (sSocket == INVALID_SOCKET) {
			cout << "Failed to connect." << endl;
			return;
		}

		auto sock = std::make_shared<MyClientSocket>(io, sSocket);
		std::string response = "Hello from client!";
		sock->write_msg(1337, std::vector<BYTE>(response.begin(), response.end()));

		io.push_socket(sock);
		});

	io.run();

}

class MySocket :public sockets::asio::socket {
public:
	using sockets::asio::socket::socket;
	void on_msg(sockets::asio::sMsg* msg)
	{
		switch (msg->wMsgID) {
		case 1337:
		{
			cout << "msg=" << std::string((const char*)msg->data, msg->wMsgLen) << endl;
			std::string response = "(Response) hello from server!";
			this->write_msg(1338, std::vector<BYTE>(response.begin(), response.end()));
		}
		break;
		}

	};
};

int main()
{
	if (!sockets::initialize()) {
		cout << "Unable to init Winsock" << endl;
		return -1;
	}
	{
		sockets::asio::io_service io;
		sockets::asio::listener listener;

		if (listener.listen(1338, sockets::asio::LOOPBACK | sockets::asio::IPv4 | sockets::asio::IPv6)) {
			cout << "listening on port " << listener.port() << endl;
		}
		else {
			cout << "Failed to listen on port " << 1338 << endl;
			cout << std::hex << WSAGetLastError() << endl;
			system("PAUSE");
			return -2;
		}

		listener.accept([&io](SOCKET sSocket) {
			auto sock = std::make_shared<MySocket>(io, sSocket);
			io.push_socket(std::dynamic_pointer_cast<sockets::asio::socket>(sock));
			});

		std::thread client_simulator(&fake_client);
		io.run();


		if (client_simulator.joinable()) {
			std::cout << "joinable" << std::endl;
			client_simulator.join();
		}
	}
	sockets::uninitialize();
}