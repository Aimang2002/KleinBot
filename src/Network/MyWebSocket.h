#ifndef MYWEBSOCKET_H
#define MYWEBSOCKET_H
#include "WebSocketHead.h"
#include <atomic>

class MyWebSocket
{
public:
    static void connectWebSocket(const std::string &url, const std::atomic<bool> &running);
};

#endif // MYWEBSOCKET_H
