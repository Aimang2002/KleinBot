#include "Log/Log.h"
#include "MessageQueue/InboundMessageQueue.h"
#include "MessageQueue/OutboundMessageQueue.h"
#include "MessageSender/QueuedMessageSender.h"
#include "Network/MyReverseWebSocket.h"
#include "Network/MyWebSocket.h"
#include "Network/OneBotHttpTransport.h"
#include "Network/TransportConfig.h"
#include "Protocol/OneBot/OneBotEventDecoder.h"
#include "Protocol/OneBot/OneBotMessageEncoder.h"

#include "../Library/nlohmann/json.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: transport_contract_driver MODE PORT [EVENT_PORT]\n";
        return 2;
    }

    const std::string mode = argv[1];
    const std::string primaryPort = argv[2];
    TransportConfig config;
    config.connectTimeoutMs = 1000;
    config.requestTimeoutMs = 3000;
    config.maxBodyBytes = 65536;

    if (mode == "forward_websocket")
    {
        config.mode = TransportMode::ForwardWebSocket;
        config.forwardWebSocket.host = "127.0.0.1";
        config.forwardWebSocket.port = primaryPort;
        config.forwardWebSocket.path = "/onebot";
        config.forwardWebSocket.authToken = "contract-token";
    }
    else if (mode == "reverse_websocket")
    {
        config.mode = TransportMode::ReverseWebSocket;
        config.reverseWebSocket.bindHost = "127.0.0.1";
        config.reverseWebSocket.bindPort = static_cast<unsigned short>(std::stoul(primaryPort));
        config.reverseWebSocket.path = "/onebot";
        config.reverseWebSocket.authToken = "contract-token";
    }
    else if (mode == "http" && argc >= 4)
    {
        config.mode = TransportMode::Http;
        config.http.apiBaseUrl = "http://127.0.0.1:" + primaryPort;
        config.http.apiAuthToken = "contract-token";
        config.http.eventBindHost = "127.0.0.1";
        config.http.eventBindPort = static_cast<unsigned short>(std::stoul(argv[3]));
        config.http.eventPath = "/onebot/events";
        config.http.eventAuthToken = "contract-token";
    }
    else
    {
        std::cerr << "invalid mode\n";
        return 2;
    }

    InboundMessageQueue inboundQueue;
    OutboundMessageQueue outboundQueue;
    QueuedMessageSender sender(outboundQueue);
    OneBotEventDecoder eventDecoder;
    OneBotMessageEncoder messageEncoder("send_private_msg", "send_group_msg");
    std::atomic<bool> running{true};

    sender.send_private(42, TextMessage{"contract-outbound"});

    std::thread transportThread([&]() {
        switch (config.mode)
        {
        case TransportMode::ForwardWebSocket:
            MyWebSocket::connectWebSocket(
                config.forwardWebSocket, inboundQueue, outboundQueue,
                eventDecoder, messageEncoder, running);
            break;
        case TransportMode::ReverseWebSocket:
            MyReverseWebSocket::connectReverseWebSocket(
                config.reverseWebSocket, inboundQueue, outboundQueue,
                eventDecoder, messageEncoder, running);
            break;
        case TransportMode::Http:
            OneBotHttpTransport::run(
                config, inboundQueue, outboundQueue, eventDecoder, messageEncoder, running);
            break;
        }
    });

    std::optional<InboundMessage> inboundMessage;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline)
    {
        inboundMessage = inboundQueue.tryPop();
        if (inboundMessage)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    running.store(false);
    if (transportThread.joinable())
    {
        transportThread.join();
    }

    if (!inboundMessage)
    {
        std::cerr << "contract event was not received\n";
        Log::getInstance().shutdown();
        return 1;
    }

    const nlohmann::json result = {
        {"mode", mode},
        {"user_id", inboundMessage->user_id},
        {"message_type", inboundMessage->message_type},
        {"plain_text", inboundMessage->plain_text}
    };
    std::cout << "CONTRACT_EVENT " << result.dump() << std::endl;
    Log::getInstance().shutdown();
    return 0;
}
