#ifndef MYREVERSEWEBSOCKET_H
#define MYREVERSEWEBSOCKET_H
#include "WebSocketHead.h"

extern ConfigManager &CManager;

class MyReverseWebSocket
{
public:
    static void connectReverseWebSocket();
    static std::string messageEncapsulation(const std::string message, const std::string messageEndpoint);

private:
};

#endif // REVERSEWEBSOCKET_H