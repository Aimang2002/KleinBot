/************************
* 该模块是用于运维警告使用的，主要是针对服务器的一些个性要求做上报或者获取服务器信息。

************************/

#pragma once
#include <iostream>
#include "../Log/Log.h"
#include <vector>
#include <sstream>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

class ComputerStatus
{
public:
    ComputerStatus();
    std::string getInet4();
    std::string getInet6();
    std::string getPublicIP4();
    ~ComputerStatus();

private:
    std::string Command(const std::string command);
};