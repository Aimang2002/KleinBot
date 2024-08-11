#include "ConfigManager.h"

ConfigManager::ConfigManager(string configPath)
{
    this->configuation = new unordered_map<string, string>;
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

void ConfigManager::readConfig(string configPath)
{
    ifstream file(configPath);
    if (!file.is_open())
    {
        LOG_ERROR("配置文件不存在！无法启动该程序");
        exit(-1);
    }
    string content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
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
                        (*this->configuation)[item->name.GetString()] = to_string(item->value.GetInt());
                    }
                    else if (item->value.IsUint())
                    {
                        (*this->configuation)[item->name.GetString()] = to_string(item->value.GetUint());
                    } // 这里还可以添加其他类型
                }
            }
        }
    }
}

bool ConfigManager::refreshConfiguation(string configPath)
{
    try
    {
        if (this->configuation != nullptr)
        {
            delete this->configuation;
            this->configuation = nullptr;
        }
        this->configuation = new unordered_map<string, string>;
        this->readConfig(configPath);
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return false;
    }
}

string ConfigManager::configVariable(string variable)
{
    string result = (*this->configuation)[variable];
    if (result.size() < 1)
    {
        LOG_FATAL("在寻找变量“" + variable + "”时不存在，可能会导致程序崩溃...");
        return string();
    }
    return result;
}