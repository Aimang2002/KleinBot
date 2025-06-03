#include "ConfigManager.h"

ConfigManager::ConfigManager(std::string configPath)
{
    this->configuation = new std::unordered_map<std::string, std::string>;
    this->readConfig(configPath);

#ifdef DEBUG
    LOG_INFO("配置文件如下：\n-----------------");
    for (auto it = this->configuation->begin(); it != this->configuation->end(); it++)
    {
        std::cout << it->first << "\t" << it->second << std::endl;
    }
    LOG_INFO("-----------------");
#endif
    LOG_INFO("配置管理器初始化完成！");
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

    Document document;
    document.Parse(content.c_str());

    if (document.IsObject())
    {
        for (Value::ConstMemberIterator category = document.MemberBegin(); category != document.MemberEnd(); ++category)
        {
            if (category->value.IsObject())
            {
                for (Value::ConstMemberIterator item = category->value.MemberBegin(); item != category->value.MemberEnd(); ++item)
                {
                    if (item->value.IsString())
                    {
                        (*this->configuation)[item->name.GetString()] = item->value.GetString();
                    }
                    else if (item->value.IsBool())
                    {
                        (*this->configuation)[item->name.GetString()] = item->value.GetBool() ? "true" : "false";
                    }
                    else if (item->value.IsInt())
                    {
                        (*this->configuation)[item->name.GetString()] = std::to_string(item->value.GetInt());
                    }
                    else if (item->value.IsUint())
                    {
                        (*this->configuation)[item->name.GetString()] = std::to_string(item->value.GetUint());
                    } // 这里还可以添加其他类型
                }
            }
        }
    }

    // 检查参数是否合法
    this->validDetection();
}

bool ConfigManager::refreshConfiguation(std::string configPath)
{
    try
    {
        if (this->configuation != nullptr)
        {
            delete this->configuation;
            this->configuation = nullptr;
        }
        this->configuation = new std::unordered_map<std::string, std::string>;
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
    std::string result = (*this->configuation)[variable];
    if (result.size() < 1)
    {
        LOG_FATAL("在寻找变量“" + variable + "”时不存在，可能会导致程序崩溃...");
        return std::string();
    }
    return result;
}

void ConfigManager::validDetection()
{
    // 检查IP和端口
    std::string address[] = {"VITS_API_URL", "WEBSOCKET_MESSAGE_IP", "REVERSEWEBSOCKET_MESSAGE_IP"};
    for (std::string &element : address)
    {
        auto result = this->configuation->find(element);
        if (result != this->configuation->end())
        {
            std::string str = result->second;
            size_t index = str.find('.');
            while (str.size() > 0)
            {
                try
                {
                    if (std::stoi(str.substr(0, index)) > 255)
                    {
                        LOG_FATAL("IP设置有误，请检查配置文件！\n错误的IP：" + result->second);
                        exit(-1);
                    }
                    if (index == str.npos)
                    {
                        break;
                    }
                    str = str.substr(index + 1);
                    index = str.find('.');
                }
                catch (const std::exception &e)
                {
                    // LOG_DEBUG(e.what());
                    LOG_FATAL("严重的错误！IP为" + result->second);
                    exit(-1);
                }
            }
        }
        else
        {
            LOG_WARNING(result->first + "参数未找到!");
        }
    }

    std::string port[] = {"VITS_API_PORT", "WEBSOCKET_MESSAGE_PORT", "REVERSEWEBSOCKET_MESSAGE_PORT"};
    for (std::string &element : port)
    {
        auto result = this->configuation->find(element);
        if (result != this->configuation->end())
        {
            try
            {
                if (std::stoi(result->second) > 65535)
                {
                    LOG_FATAL("接口设置不正确：取值范围应该是0~65535，当前值为：" + result->second);
                    exit(-1);
                }
            }
            catch (const std::exception &e)
            {
                LOG_FATAL(result->first + "的" + result->second + "为非法值！");
                exit(-1);
            }
        }
    }

    // 检查上下文最大长度、存活时间、单次发送的最大长度
    std::string v[] = {"MESSAGE_SURVIVAL_TIME", "MODEL_SIGLE_TOKEN_MAX", "MODEL_SIGLE_TOKEN_MAX"};
    for (auto &element : v)
    {
        auto result = this->configuation->find(element);
        if (result != this->configuation->end())
        {
            try
            {
                if (std::stoll(result->second) > INT_MAX)
                {
                    LOG_WARNING(result->first + "值设置的范围超过" + std::to_string(INT_MAX));
                    result->second = std::to_string(INT_MAX - 1);
                }
                else if (std::stoll(result->second) < 0)
                {
                    LOG_WARNING(result->first + "禁止设置为负数！\t当前参数：" + result->second);
                    exit(-1);
                }
            }
            catch (const std::exception &e)
            {
                LOG_FATAL(result->first + "填入了错误的参数\t当前参数：" + result->second);
                exit(-1);
            }
        }
    }

    // 超参数检查
    std::string Hyperparameter[] = {"temperature", "presence_penalty", "frequency_penalty", "top_p"};
    for (auto &element : Hyperparameter)
    {
        auto result = this->configuation->find(element);
        if (result != this->configuation->end())
        {
            auto Lambda = [&]()
            {
                LOG_WARNING(result->first + "设置有误！将自动调整为0。错误的参数：" + result->second);
                result->second = "0";
            };
            try
            {
                if (element == "temperature")
                {
                    if (std::stof(result->second) > 2.0 || std::stof(result->second) < 0.0)
                    {
                        Lambda();
                    }
                }
                else if (element == "presence_penalty" || element == "frequency_penalty")
                {
                    if (std::stof(result->second) > 2.0 || std::stof(result->second) < -2.0)
                    {
                        Lambda();
                    }
                }
                else if (element == "top_p")
                {
                    if (std::stof(result->second) > 1.0 || std::stof(result->second) < 0.0)
                    {
                        Lambda();
                    }
                }
            }
            catch (const std::exception &e)
            {
                LOG_FATAL(result->first + "填入了错误的参数\n当前参数为：" + result->second);
                exit(-1);
            }
        }
    }
    // LOG_INFO("配置文件参数无异常。");
}