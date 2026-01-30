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
    bool Noise_intercept(const std::string &message)
    {
        try
        {
            nlohmann::json json_data = nlohmann::json::parse(message);
            if(json_data.is_object()&&json_data.contains("post_type")&&
            json_data["post_type"].is_string()&& json_data["post_type"].get<std::string>() == "meta_event")
            {
                return true;
            }
        }
        catch(const std::exception &e)
        {
            LOG_ERROR("噪声拦截失败，错误信息：" + std::string(e.what()));
            return true;
        }
        return false;
    }
}