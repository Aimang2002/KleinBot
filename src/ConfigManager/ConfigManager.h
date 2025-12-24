#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <iostream>
#include <string>
#include <unordered_map>

class ConfigManager
{
public:
    static ConfigManager &getInstance();
    bool refreshConfiguation(const std::string configPath = "config.json");
    std::string configVariable(const std::string varriable);

private:
    ConfigManager(std::string configPath);
    ConfigManager(ConfigManager const &) = delete;
    ConfigManager &operator=(ConfigManager const &) = delete;
    void readConfig(const std::string configPath);
    void validDetection();

private:
    std::unordered_map<std::string, std::string> configuration;
    static ConfigManager *instance;
};

#endif
