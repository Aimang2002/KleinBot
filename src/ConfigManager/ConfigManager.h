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

typedef long long UINT64;

using namespace rapidjson;
using namespace std;

class ConfigManager
{
public:
    ConfigManager(string configPath);
    bool refreshConfiguation(const string configPath);
    string configVariable(const string varriable);

private:
    void readConfig(const string configPath);

private:
    unordered_map<string, string> *configuation;
};

#endif
