#ifndef UTILS_HPP
#define UTILS_HPP

#include <iostream>
#include <string>
#include "../../Library/nlohmann/json.hpp"
#include "../Log/Log.h"

namespace utils
{
    /**
     * @brief 噪声拦截
     * @param message 消息
     * @return true为拦截，false为不拦截
     */
    inline bool Noise_intercept(const std::string &message)
    {
        try
        {
            nlohmann::json json_data = nlohmann::json::parse(message);
            if (json_data.is_object() && json_data.contains("post_type") &&
                json_data["post_type"].is_string() && json_data["post_type"].get<std::string>() == "meta_event")
            {
                return true;
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("噪声拦截失败，错误信息：" + std::string(e.what()));
            return true;
        }
        return false;
    }

    // 命令prompt提取
    inline std::string commandPromptDraw(const std::string prompt)
    {
        std::string commandFrefix = prompt.substr(0, 10);
        std::string result;
        if (commandFrefix.find(":") != std::string::npos)
        {
            result = prompt.substr(commandFrefix.find(":") + 1);
        }
        else if (commandFrefix.find("：") != std::string::npos)
        {
            result = prompt.substr(commandFrefix.find("：") + 3);
        }
        return result;
    }

    // 分离Command和Prompt
    inline std::string CP_split(std::string message, std::string command)
    {
        int command_size = command.size();
        std::string res;
        std::string subs = message.substr(0, command_size + 3);
        if (message.size() < command_size)
        {
            return "";
        }

        if (subs.find(command + ":") != subs.npos)
        {
            res = message.substr(command_size + 1);
        }
        else if (subs == (command + "："))
        {
            res = message.substr(command_size + std::string("：").size());
        }
        return res;
    }

    // 判断命令是否存在
    inline bool check_command_exists(const std::string &message, const std::string command)
    {
        int command_size = command.size();
        std::string res;
        std::string subs = message.substr(0, command_size + 3);
        if (message.size() < command_size)
        {
            return false;
        }

        if (subs.find((command + ":")) != subs.npos || subs.find((command + "：")) != subs.npos)
        {
            return true;
        }
        return false;
    }

    // 去除前后空白字符（空格、制表符、换行等）
    inline std::string trim(const std::string &str)
    {
        size_t start = 0;
        while (start < str.size() && std::isspace(static_cast<unsigned char>(str[start])))
        {
            ++start;
        }

        if (start == str.size())
        {
            return "";
        }

        size_t end = str.size() - 1;
        while (end > start && std::isspace(static_cast<unsigned char>(str[end])))
        {
            --end;
        }
        return str.substr(start, end - start + 1);
    }
}

#endif // UTILS_HPP