/*
#include "Network.h"

Network *Network::instance = nullptr;
stack<std::string> Network::messageTask = stack<std::string>();
std::mutex Network::__mutex;

Network &Network::getInstance()
{
	if (instance == nullptr)
	{
		instance = new Network;
		// 设置子线程，开启反向WebSocket
		std::thread t(reverseWebSocket);
		t.detach();
		return *instance;
	}
	return *instance;
}

void Network::setTaskMessage(string message, string messageEndpoint)
{
	string format = R"({"action":")" + messageEndpoint + R"(","params":)";
	std::lock_guard<std::mutex> lock(__mutex);
	messageTask.push(format + message);
}

void Network::reverseWebSocket()
{
	unsigned short palpitate = 0; // 心跳机制

	while (true)
	{
		std::string const address = CManager.configVariable("SEND_MESSAGE_IP").c_str();
		unsigned short const port = stoi(CManager.configVariable("SEND_MESSAGE_PORT"));

		net::io_context ioc{1};
		tcp::acceptor acceptor{ioc, {tcp::v4(), port}};
		tcp::socket socket{ioc};

		acceptor.accept(socket);

		try
		{
			websocket::stream<tcp::socket> ws{std::move(socket)};
			ws.accept();
			for (;;)
			{
				string message;
				if (!messageTask.empty()) // 若消息队列不为空
				{
					{
						std::lock_guard<std::mutex> lock(__mutex);
						message = messageTask.top();
						messageTask.pop();
					}
#ifdef DEBUG
					cout << "发送数据：" << message << endl;
#endif
					ws.text(ws.got_text());
					ws.write(net::buffer(message));
					cout << "send over!" << endl;
				}
				else
				{
					sleep(1); // 休眠一秒
					palpitate++;
					if (palpitate > 10) // 当休眠达到十次，将进行一个心跳
					{
						ws.text(ws.got_text());
						ws.write(net::buffer("ping")); // 发送心跳
					}
				}
				// std::string message = R"({"action":"send_private_msg","params":{"user_id":1472807646,"message":"Hello, this is a private message!"},"echo":"your_unique_echo"})";
			}
		}
		catch (beast::system_error const &se)
		{
			std::cerr << "Error: " << se.what() << std::endl;
		}

		LOG_FATAL("反向ws已失联，10秒后将重新连接...");
		sleep(10); // 休眠10后重连
	}
}

*/