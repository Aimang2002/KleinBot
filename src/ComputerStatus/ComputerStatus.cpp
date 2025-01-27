#include "ComputerStatus.h"

ComputerStatus::ComputerStatus()
{
}

std::string ComputerStatus::getInet4()
{
    std::string ipv4Addresses;
#if defined(__linux__) // defined(_WIN32) || defined(_WIN64)
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
            // ipv4Addresses.append("Interface:");
            // ipv4Addresses.append(ifa->ifa_name);
            ipv4Addresses.append("IPv4 Address: ");
            ipv4Addresses.append(ipAddr);
            ipv4Addresses.append("\n\n");
        }
    }
    freeifaddrs(ifAddrStruct);

#endif

#if defined(_WIN32) || defined(_WIN64)
    ULONG buflen = sizeof(IP_ADAPTER_ADDRESSES);
    IP_ADAPTER_ADDRESSES *pAddresses = (IP_ADAPTER_ADDRESSES *)malloc(buflen);

    if (GetAdaptersAddresses(AF_INET, 0, NULL, pAddresses, &buflen) == ERROR_BUFFER_OVERFLOW)
    {
        free(pAddresses);
        pAddresses = (IP_ADAPTER_ADDRESSES *)malloc(buflen);
    }

    if (GetAdaptersAddresses(AF_INET, 0, NULL, pAddresses, &buflen) == NO_ERROR)
    {
        for (IP_ADAPTER_ADDRESSES *pCurr = pAddresses; pCurr; pCurr = pCurr->Next)
        {
            for (IP_ADAPTER_UNICAST_ADDRESS *pUnicast = pCurr->FirstUnicastAddress; pUnicast != NULL; pUnicast = pUnicast->Next)
            {
                ipv4Addresses.append("IPv4 Address: ");
                SOCKADDR_IN *sa_in = (SOCKADDR_IN *)pUnicast->Address.lpSockaddr;
                ipv4Addresses.append(inet_ntoa(sa_in->sin_addr));
                ipv4Addresses.append("\n\n");
            }
        }
    }

    free(pAddresses);
#endif
    return ipv4Addresses;
}

std::string ComputerStatus::getInet6()
{
    std::string ipv6Addresses;
#if defined(__linux__)
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        return "";
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET6)
        {
            char addr[INET6_ADDRSTRLEN];
            void *tempAddrPtr = &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
            inet_ntop(AF_INET6, tempAddrPtr, addr, INET6_ADDRSTRLEN);
            // ipv6Addresses += ifa->ifa_name;
            ipv6Addresses += "IPv6 Address：";
            ipv6Addresses += addr;
            ipv6Addresses.append("\n\n");
        }
    }

    freeifaddrs(ifaddr);
#endif

#if defined(_WIN32) || defined(_WIN64)
    ULONG buflen = sizeof(IP_ADAPTER_ADDRESSES);
    IP_ADAPTER_ADDRESSES *pAddresses = (IP_ADAPTER_ADDRESSES *)malloc(buflen);

    if (GetAdaptersAddresses(AF_INET6, 0, NULL, pAddresses, &buflen) == ERROR_BUFFER_OVERFLOW)
    {
        free(pAddresses);
        pAddresses = (IP_ADAPTER_ADDRESSES *)malloc(buflen);
    }

    if (GetAdaptersAddresses(AF_INET6, 0, NULL, pAddresses, &buflen) == NO_ERROR)
    {
        for (IP_ADAPTER_ADDRESSES *pCurr = pAddresses; pCurr; pCurr = pCurr->Next)
        {
            for (IP_ADAPTER_UNICAST_ADDRESS *pUnicast = pCurr->FirstUnicastAddress; pUnicast != NULL; pUnicast = pUnicast->Next)
            {
                if (pUnicast->Address.lpSockaddr->sa_family == AF_INET6)
                {
                    ipv6Addresses.append("IPv6 Address: ");
                    char addressString[INET6_ADDRSTRLEN];
                    sockaddr_in6 *ipv6Addr = (sockaddr_in6 *)pUnicast->Address.lpSockaddr;
                    inet_ntop(AF_INET6, &(ipv6Addr->sin6_addr), addressString, sizeof(addressString));
                    ipv6Addresses.append(addressString);
                    ipv6Addresses.append("\n\n");
                }
            }
        }
    }

    free(pAddresses);
#endif
    return ipv6Addresses;
}

std::size_t callback(const char *in, std::size_t size, std::size_t num, std::string *out)
{
    const std::size_t totalBytes(size * num);
    out->append(in, totalBytes);
    return totalBytes;
}
std::string ComputerStatus::getPublicIP()
{
    std::string IP;
    std::vector<std::pair<std::string, std::string>> url = {
        std::make_pair("http://ip.3322.net", "IPv4 Address: "),
        std::make_pair("http://ipv6.icanhazip.com", "IPv6 Address: ")};

    for (int index = 0; index < 2; ++index)
    {
        CURL *curl = curl_easy_init();
        std::string response;

        if (curl)
        {
            curl_easy_setopt(curl, CURLOPT_URL, url[index].first.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK)
            {
                std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << '\n';
            }
            else
            {
                IP.append(url[index].second + response + "\n");
            }

            curl_easy_cleanup(curl);
        }
        else
        {
            return "Failed to initialize CURL";
        }
    }

    return IP;
}

ComputerStatus::~ComputerStatus()
{
}