#include "ConfigManager.h"

ConfigManager::ConfigManager(std::string configPath)
{
    this->configuation = new std::unordered_map<std::string, std::string>;
    this->readConfig(configPath);

#ifdef DEBUG
    LOG_INFO("配置文件如下：\n-----------------");
    for (auto it = this->configuation->begin(); it != this->configuation->end(); it++)
    {
        cout << it->first << "\t" << it->second << endl;
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
        LOG_ERROR("配置文件不存在！无法启动该程序");
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