#pragma once
#include "bitflg.hpp"

namespace sockets {
	enum internet_protocols { ipv4, ipv6 };

#if !defined(SOCKET_INITIALIZATION_ROUTINES)
#define SOCKET_INITIALIZATION_ROUTINES
	bool _inline initialize()
	{
		WSADATA wsa;
		int iWSAError, nTries = 0;
		while ((iWSAError = WSAStartup(MAKEWORD(2, 2), &wsa)) != 0 && nTries++ < 5) {
			switch (iWSAError) {
			case WSASYSNOTREADY:
			case WSAEPROCLIM:
				Sleep(5000);
				break;
			default: //WSAVERNOTSUPPORTED, WSAEFAULT, WSAVERNOTSUPPORTED, WSAEINPROGRESS
				Sleep(1000);
			}
		}
		return iWSAError == 0;
	};

	void _inline uninitialize()
	{
		::WSACleanup(); //return ::WSACleanup() == 0;
	};
#endif

	namespace asio {
		class io_service;

		enum socket_states:LONG {
			connected,
			disconnected,
			disconnecting
		};

		enum completion_state {
			partially_completed,
			completed_successfully,
			abruptly_disconnected
		};

		enum internal_msg_types {
			id_disconnect,
			id_ping,
			id_pong,
			id_internal_msg_max
		};

#pragma pack(push, 1)
		struct sRawNetworkMsgHdr {
			WORD wMsgID;
			ULONG wMsgLen;
		};

		struct sMsg :public sRawNetworkMsgHdr {
			BYTE data[ANYSIZE_ARRAY];
		};
#pragma pack(pop)
		struct sNetworkMsgWrite{
			std::vector<BYTE> msg;
			uint32_t pos;
			std::function<void(bool bCompleted)> cb;
		};

		class socket :public std::enable_shared_from_this<socket> {
		public:
			socket(io_service& io_svc, SOCKET sSocket);
			socket(socket&& other);
			socket(io_service& io_svc, socket&& other);
			socket(const socket&) = delete;
			virtual ~socket();

			void shutdown();
			void close();

			bool disconnected() const { return socket_state == socket_states::disconnected; };
			bool connected() const { return !disconnected(); };
			bool is_state_connected() const { return socket_state == socket_states::connected; };
			bool disconnecting() const { return socket_state == socket_states::disconnecting; };
			void disconnect(bool graceful = true);
			//virtual void on_bytes_read(uint32_t len, completion_state state);
			//virtual void on_bytes_sent(uint32_t len, completion_state state);

			void on_bytes_read(uint8_t* data, int32_t len);
			void on_bytes_written(sNetworkMsgWrite* msg, int written);
			//note: can't write any new messages when disconnecting

			virtual void on_msg(sMsg* msg);

			bool write_msg(USHORT id, const std::vector<BYTE>& msg = std::vector<BYTE>(), std::function<void(bool bCompleted)> cb = nullptr);
		private:
			void process_read(uint8_t* data, int32_t len);
			void process_internal_msg(sMsg* msg);
			void check_msg_completion();

			struct {
				struct {
					std::vector<BYTE> msg;
				}i;
				struct {
					std::mutex m;
					std::list<sNetworkMsgWrite> msgs;
				}o;
			}io;

			size_t queue_tickets = NULL;
			size_t current_queue_ticket = 1;
			size_t create_queue_ticket();

			friend class io_service;
			io_service& io_svc;
			SOCKET sSocket;
			socket_states socket_state;
			std::chrono::time_point<std::chrono::steady_clock> grace_period_start;
		};

		enum io_types {
			io_rd,
			io_wr
		};

		enum listener_flags {
			LOOPBACK = BitFlags::option1,
			ANY = BitFlags::option2,
			IPv4 = BitFlags::option3,
			IPv6 = BitFlags::option4
		};

		listener_flags _inline operator |(const listener_flags& first, const listener_flags& other) {
			return static_cast<listener_flags>(static_cast<DWORD>(first) | static_cast<DWORD>(other));
		};

		class listener {
		public:
			listener();
			~listener();
			bool listen(WORD wPort, listener_flags flags = ANY | IPv4);
			void accept(std::function<void(SOCKET)> f);
			bool status(internet_protocols protocol) const { return IPStatus[protocol]; };
			void close(bool from_inside_lthread = false);
			bool disconnected() const { return bDisconnected; };
			WORD port() const { return this->wPort; };
		private:
			void run();
			std::thread t;
			bool bStop, bDisconnected, IPStatus[2];
			int iSockets;
			WORD wPort;
			SOCKET sSockets[FD_SETSIZE];
			std::function<void(SOCKET)> func;
		};

		//only allows you to define a single callback function per instance.
		class connector {
		public:
			connector(std::function<void(SOCKET)> default_func = nullptr);
			~connector();
			bool connect(const std::string& host, WORD port, const std::chrono::milliseconds& duration = std::chrono::seconds(20));
			bool connect(const std::string& host, WORD port, const std::chrono::milliseconds& duration, std::function<void(SOCKET)> callback);
			size_t size() const; //queue'd socket size(not yet complete)
			void wait();
			void cancel();
		private:
			mutable std::mutex m;
			std::list<std::pair<std::pair<SOCKET, std::function<void(SOCKET)>>, std::chrono::steady_clock::time_point>> s;
			bool bStop;
			void run();
			void check();
			bool check(FD_SET& s);
			void process(std::list<SOCKET>& valid, FD_SET& w);
			std::thread t;
			std::function<void(SOCKET)> default_func;
		};

		class io_service{
		public:
			io_service(int nWorkers = 25);
			~io_service();

			void poll(int timeout = 100);
			void run();
			void quit(bool graceful = false);
			void wait();

			void enumerate_sockets(std::function<void(std::shared_ptr<socket>&)> f);

			void push_socket(std::shared_ptr<socket> socket);
		private:
			std::chrono::milliseconds disconnect_grace_period;
			friend class socket;
			void worker();
			void on_network_msg(std::weak_ptr<socket> socket, std::vector<BYTE>&& msg);
			void process_io(io_types io_type, std::shared_ptr<socket> socket);

			bool bQuit, bQuitted, bGracefulQuit;
			std::list<std::thread> workers;
			mutable std::recursive_mutex m_sockets;
			std::list<std::shared_ptr<socket>> sockets;
			struct {
				std::vector<WSAPOLLFD> pool;
			}chk;

			struct {
				std::mutex m_queue_lock;
				std::queue<std::pair<std::weak_ptr<socket>, std::vector<BYTE>>> msgs;
				std::condition_variable cv;
			}completed_io;
		};

	}
}