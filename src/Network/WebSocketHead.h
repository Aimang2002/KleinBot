#ifndef WEBSOCKETHEAD_H
#define WEBSOCKETHEAD_H

#include <iostream>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include "../ConfigManager/ConfigManager.h"
#include "../Log/Log.h"
#include "../MessageQueue/MessageQueue.h"

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
using namespace boost::beast;
using namespace boost::asio;
using tcp = ip::tcp;

#endif // WEBSOCKETHEAD_H