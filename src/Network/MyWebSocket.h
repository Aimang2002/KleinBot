#ifndef MYWEBSOCKET_H
#define MYWEBSOCKET_H
#include "WebSocketHead.h"

extern ConfigManager &CManager;

class MyWebSocket
{
public:
    static void connectWebSocket(const std::string _url);

private:
    static bool filterMessageType(std::string originalMessage); // trus is filter
};

#endif // MYWEBSOCKET_H