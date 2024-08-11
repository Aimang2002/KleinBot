#include "ComputerStatus.h"

ComputerStatus::ComputerStatus()
{
}

std::string ComputerStatus::Command(const std::string command)
{
    std::FILE *pipe = popen(command.c_str(), "r");
    if (!pipe)
    {
        LOG_ERROR(command + "命令无法执行...");
        return std::string();
    }

    // 读取数据
    char buffer[512];
    std::string ss;

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
        ss += buffer;
    }

    pclose(pipe);

    return ss;
}

std::string ComputerStatus::getInet4()
{
    std::string ipv4Addresses;
    struct ifaddrs *ifAddrStruct = nullptr;
    if (getifaddrs(&ifAddrStruct) == -1)
    {
        LOG_ERROR("无法获取接口地址...");
        return std::string();
    }

    for (struct ifaddrs *ifa = ifAddrStruct; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == nullptr)
        {
            continue;
        }

        if (ifa->ifa_addr->sa_family == AF_INET)
        {
            struct sockaddr_in *addr = reinterpret_cast<struct sockaddr_in *>(ifa->ifa_addr);
            char ipAddr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(addr->sin_addr), ipAddr, INET_ADDRSTRLEN);
            // std::cout << "Interface: " << ifa->ifa_name << " - IPv4 Address: " << ipAddr << std::endl;
            ipv4Addresses.append("Interface:");
            ipv4Addresses.append(ifa->ifa_name);
            ipv4Addresses.append("\t  IPv4 Address: ");
            ipv4Addresses.append(ipAddr);
            ipv4Addresses += '\n';
        }
    }
    freeifaddrs(ifAddrStruct);

    return ipv4Addresses;
}

std::string ComputerStatus::getInet6()
{
    std::istringstream iss(this->Command("ifconfig | grep inet6"));
    std::string ipv6Addresses;
    std::string line;

    while (std::getline(iss, line))
    {
        std::size_t inet6Pos = line.find("inet6 ");
        if (inet6Pos != std::string::npos)
        {
            inet6Pos += 6; // "inet6 "长度为6
            std::size_t spacePos = line.find(' ', inet6Pos);
            if (spacePos != std::string::npos)
            {
                ipv6Addresses += line.substr(inet6Pos, spacePos - inet6Pos);
                ipv6Addresses += "\n\n";
            }
        }
    }

    return ipv6Addresses;
}

std::string ComputerStatus::getPublicIP4()
{
    char buffer[128];
    std::string IP;
    FILE *fp;

    // 获取IPv4
    fp = popen("curl 4.ipw.cn", "r");
    if (fp == NULL)
    {
        LOG_WARNING("popen failed!");
        return std::string("获取失败！");
    }

    // 从管道中读取命令的输出
    if (fgets(buffer, sizeof(buffer), fp) != NULL)
    {
        IP += std::string("IPv4:") + buffer;
    }
    // 关闭管道
    pclose(fp);

    IP += "\n";

    // 获取IPv6
    fp = popen("curl 6.ipw.cn", "r");
    if (fp == NULL)
    {
        LOG_WARNING("popen failed!");
        return std::string("获取失败！");
    }

    if (fgets(buffer, sizeof(buffer), fp) != NULL)
    {
        IP += std::string("IPv6:") + buffer;
    }
    pclose(fp);

    return IP;
}

ComputerStatus::~ComputerStatus()
{
}