#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <unordered_map>
#include "../Log/Log.h"
#include "../../Library/rapidjson/document.h"
#include "../../Library/rapidjson/filereadstream.h"

using namespace rapidjson;
// using namespace std;

class ConfigManager
{
public:
    ConfigManager(std::string configPath);
    bool refreshConfiguation(const std::string configPath);
    std::string configVariable(const std::string varriable);

private:
    void readConfig(const std::string configPath);

private:
    std::unordered_map<std::string, std::string> *configuation;
};

#endif
