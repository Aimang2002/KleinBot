/************************
* 该模块是用于运维警告使用的，主要是针对服务器的一些个性要求做上报或者获取服务器信息。

************************/

#ifndef COMPUTERSTATUS_H
#define COMPUTERSTATUS_H

#include <iostream>

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

#endif // COMPUTERSTATUS_H