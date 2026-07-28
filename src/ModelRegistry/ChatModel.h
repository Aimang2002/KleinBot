#ifndef CHATMODEL_H
#define CHATMODEL_H

#include <unordered_set>
#include <vector>
#include <string>

struct ChatModelCapabilities
{
    bool vision = false;
};

struct ChatModel
{
    std::unordered_set<std::string> modelList;
    std::string api_key;
    std::string endpoint;
    std::string api_standard;
    ChatModelCapabilities capabilities;
};

#endif // CHATMODEL_H
