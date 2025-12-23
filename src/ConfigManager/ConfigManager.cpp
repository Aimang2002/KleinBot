#include "ConfigManager.h"
#include "../Log/Log.h"
#include "../../Library/nlohmann/json.hpp"
#include <fstream>
#include <limits.h>

ConfigManager::ConfigManager(std::string configPath)
{
    this->readConfig(configPath);
    LOG_INFO("配置管理器初始化完成！");
}

ConfigManager &ConfigManager::getInstance()
{
    static ConfigManager instance("config.json");
    return instance;
}

void ConfigManager::readConfig(std::string configPath)
{
    std::ifstream file(configPath);
    if (!file.is_open())
    {
        LOG_FATAL("配置文件不存在！无法启动该程序");
        exit(-1);
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    nlohmann::json document;
    try
    {
        document = nlohmann::json::parse(content);
    }
    catch (const std::exception &e)
    {
        LOG_FATAL("配置文件格式错误！无法启动该程序，error：" + std::string(e.what()));
        exit(-1);
    }

    if (!document.is_object())
        return;

    // 扁平化存储所有键值（支持你的多层结构）
    for (auto &[cat_key, cat_val] : document.items())
    {
        if (cat_val.is_object())
        {
            for (auto &[item_key, item_val] : cat_val.items())
            {
                if (item_val.is_string())
                {
                    (this->configuration)[item_key] = item_val.get<std::string>();
                }
                else if (item_val.is_boolean())
                {
                    (this->configuration)[item_key] = item_val.get<bool>() ? "true" : "false";
                }
                else if (item_val.is_number_integer())
                {
                    (this->configuration)[item_key] = std::to_string(item_val.get<long long>());
                }
                else if (item_val.is_number_float())
                {
                    (this->configuration)[item_key] = std::to_string(item_val.get<double>());
                }
            }
        }
    }

    this->validDetection();
}

bool ConfigManager::refreshConfiguation(std::string configPath)
{
    try
    {
        if (!this->configuration.empty())
        {
            this->configuration.clear();
        }
        this->readConfig(configPath);
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return false;
    }
}

std::string ConfigManager::configVariable(std::string variable)
{
    std::string result = (this->configuration)[variable];
    if (result.size() < 1)
    {
        LOG_FATAL("在寻找变量“" + variable + "”时不存在，可能会导致程序崩溃...");
        return std::string();
    }
    return result;
}

void ConfigManager::validDetection()
{
    auto check_ip = [this](const std::string &key)
    {
        auto it = configuration.find(key);
        if (it == configuration.end())
        {
            LOG_WARNING(key + " 参数未找到!");
            return;
        }
        std::string str = it->second;
        size_t pos = 0;
        while (!str.empty())
        {
            size_t next = str.find('.', pos);
            std::string segment = (next == std::string::npos) ? str.substr(pos) : str.substr(pos, next - pos);
            try
            {
                if (std::stoi(segment) > 255)
                {
                    LOG_FATAL("IP设置有误！错误的IP：" + it->second);
                    exit(-1);
                }
            }
            catch (...)
            {
                LOG_FATAL("严重的错误！IP为" + it->second);
                exit(-1);
            }
            if (next == std::string::npos)
                break;
            pos = next + 1;
        }
    };

    auto check_port = [this](const std::string &key)
    {
        auto it = configuration.find(key);
        if (it == configuration.end())
            return;
        try
        {
            if (std::stoi(it->second) > 65535)
            {
                LOG_FATAL("接口设置不正确：取值范围0~65535，当前值为：" + it->second);
                exit(-1);
            }
        }
        catch (...)
        {
            LOG_FATAL(key + " 为非法值！");
            exit(-1);
        }
    };

    auto check_long = [this](const std::string &key)
    {
        auto it = configuration.find(key);
        if (it == configuration.end())
            return;
        try
        {
            long long val = std::stoll(it->second);
            if (val > INT_MAX)
            {
                LOG_WARNING(key + " 值超过" + std::to_string(INT_MAX));
                it->second = std::to_string(INT_MAX - 1);
            }
            else if (val < 0)
            {
                LOG_WARNING(key + " 禁止负数！当前：" + it->second);
                exit(-1);
            }
        }
        catch (...)
        {
            LOG_FATAL(key + " 填入了错误参数：" + it->second);
            exit(-1);
        }
    };

    auto check_float = [this](const std::string &key, float min_val, float max_val)
    {
        auto it = configuration.find(key);
        if (it == configuration.end())
            return;
        try
        {
            float val = std::stof(it->second);
            if (val < min_val || val > max_val)
            {
                LOG_WARNING(key + " 设置有误！自动调整为0。原值：" + it->second);
                it->second = "0";
            }
        }
        catch (...)
        {
            LOG_FATAL(key + " 填入了错误参数：" + it->second);
            exit(-1);
        }
    };

    // IP 检查
    for (const auto &k : {"VITS_API_URL", "WEBSOCKET_MESSAGE_IP", "REVERSEWEBSOCKET_MESSAGE_IP"})
    {
        check_ip(k);
    }

    // 端口检查
    for (const auto &k : {"VITS_API_PORT", "WEBSOCKET_MESSAGE_PORT", "REVERSEWEBSOCKET_MESSAGE_PORT"})
    {
        check_port(k);
    }

    // 长整型检查（去重）
    for (const auto &k : {"MESSAGE_SURVIVAL_TIME", "MODEL_SIGLE_TOKEN_MAX"})
    {
        check_long(k);
    }

    // 超参数检查
    check_float("temperature", 0.0f, 2.0f);
    check_float("presence_penalty", -2.0f, 2.0f);
    check_float("frequency_penalty", -2.0f, 2.0f);
    check_float("top_p", 0.0f, 1.0f);
}