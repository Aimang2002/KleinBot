/************************
* 该模块是用于运维警告使用的，主要是针对服务器的一些个性要求做上报或者获取服务器信息。

************************/

#pragma once
#include <iostream>
#include "../Log/Log.h"
#include <vector>
#include <sstream>
#include <curl/curl.h>

#if defined(__linux__)
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#if defined(__WIN32) || defined(__WIN64)
#include <winsock2.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

#endif

class ComputerStatus
{
public:
    ComputerStatus();
    std::string getInet4();
    std::string getInet6();
    std::string getPublicIP();
    ~ComputerStatus();

private:
};