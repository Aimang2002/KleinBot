#ifndef MYREVERSEWEBSOCKET_H
#define MYREVERSEWEBSOCKET_H
#include "WebSocketHead.h"
#include <atomic>

class MyReverseWebSocket
{
public:
    static void connectReverseWebSocket(const std::atomic<bool> &running);

private:
};

#endif // REVERSEWEBSOCKET_H
