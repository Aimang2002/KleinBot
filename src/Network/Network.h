/*
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <stack>
#include <mutex>
#include <unistd.h>

#include <arpa/inet.h>
#include "sstream"
#include "../ConfigManager/ConfigManager.h"
using namespace std;

namespace beast = boost::beast;			// from <boost/beast.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;			// from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;		// from <boost/asio/ip/tcp.hpp>
using namespace boost::beast;
using namespace boost::asio;
using tcp = ip::tcp;

extern ConfigManager &CManager;

class Network
{
public:
	Network(const Network &) = delete;
	Network(const Network &&) = delete;
	Network &operator=(const Network &) = delete;

	static Network &getInstance();

	~Network() = delete;
	// string BuildHTTPReport(string Host, int port, string requset, string json_data);
	// int send_message(string HTTP_package);
	// 设置队列消息
	static void setTaskMessage(const string message, const string messageEndpoint);

private:
	Network() = default;
	// 正向WS连接
	static void fowardWebSocket();
	// 反向WS连接
	static void reverseWebSocket();
	// 获取队列消息
	static string getTaskMessage();

private:
	// 消息队列
	static Network *instance;
	static stack<std::string> messageTask;
	static std::mutex __mutex;
};
*/