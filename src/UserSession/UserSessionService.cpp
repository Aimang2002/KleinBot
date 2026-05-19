#include "UserSessionService.h"
#include "../ConfigManager/ConfigManager.h"
#include "../Log/Log.h"

UserSessionService::UserSessionService()
{
    this->user_messages = std::make_unique<std::unordered_map<uint64_t, Person>>();
    this->default_personality = "You are my assistant, your name is " + ConfigManager::getInstance().configVariable("QBOT_NAME") +
                                "\"},";
}

Person UserSessionService::createDefaultPerson(const uint64_t user_id)
{
    // 添加默认数据
    std::vector<std::pair<std::string, time_t>> userDefault;
    std::pair<std::string, time_t> p;
    p.first = this->users_message_format + this->default_personality;
    p.second = time(nullptr); // 获取当前时间
    userDefault.push_back(p);

    p.first = this->bot_message_format + "OK!I will use Chinses answer\"},"; // 设置assistant信息
    userDefault.push_back(p);

    // 创建用户
    Person person;
    person.user_chatHistory = userDefault;

    std::pair<std::string, std::vector<std::string>> models;
    models.first = ConfigManager::getInstance().configVariable("DEFAULT_MODEL"); // 默认模型
    models.second.push_back(ConfigManager::getInstance().configVariable("DEFAULT_MODEL_API_KEY"));
    models.second.push_back(ConfigManager::getInstance().configVariable("DEFAULT_MODEL_ENDPOINT"));
    models.second.push_back(ConfigManager::getInstance().configVariable("DEFAULT_MODEL_APISTANDARD"));
    person.user_models = models;
    person.isOpenVoiceMode = false;
    person.temperature = ConfigManager::getInstance().configVariable("temperature");
    person.frequency_penalty = ConfigManager::getInstance().configVariable("frequency_penalty");
    person.presence_penalty = ConfigManager::getInstance().configVariable("presence_penalty");

    return person;
}

void UserSessionService::ensureUserExistsUnlock(const uint64_t user_id)
{
    // 找到用户
    if (user_messages->find(user_id) != user_messages->end())
        return;

    Person p = this->createDefaultPerson(user_id);
    this->user_messages->emplace(user_id, p);
}

void UserSessionService::ensureUserExists(const uint64_t user_id)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    // 找到用户
    if (user_messages->find(user_id) != user_messages->end())
        return;
    Person p = this->createDefaultPerson(user_id);
    this->user_messages->emplace(user_id, p);
}

void UserSessionService::resetChat(const uint64_t user_id)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    auto user = this->user_messages->find(user_id);
    if (user->second.user_chatHistory.size() > 2)
    {
        // 重置对话会删除之前的所有信息，但不包括人格信息
        user->second.user_chatHistory.erase(user->second.user_chatHistory.begin() + 2, user->second.user_chatHistory.end());
        this->user_messages->find(user_id)->second = user->second;
    }
}

std::string UserSessionService::getModelName(uint64_t user_id)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    auto res = this->user_messages->find(user_id);
    return res->second.user_models.first;
}

void UserSessionService::setPersonality(const uint64_t user_id, const std::string &Personality)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    auto user = this->user_messages->find(user_id);
    user->second.user_chatHistory[0].first = (this->system_message_format + Personality + "\"},");
    user->second.user_chatHistory[0].second = time(nullptr);
    user->second.user_chatHistory[1].first = (this->bot_message_format + "OK!I will use Chinses answer \"},");
    user->second.user_chatHistory[1].second = time(nullptr);
    this->user_messages->find(user_id)->second = user->second;
}

void UserSessionService::switchModel(const uint64_t user_id, const std::pair<std::string, std::vector<std::string>> &newModel)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    this->user_messages->find(user_id)->second.user_models = newModel;
}

void UserSessionService::voiceSwtich(const uint64_t user_id, const bool tag)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    auto user = this->user_messages->find(user_id);
    user->second.isOpenVoiceMode = tag;
}

bool UserSessionService::isVoiceModel(const uint64_t user_id)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    return this->user_messages->find(user_id)->second.isOpenVoiceMode;
}

std::string UserSessionService::removePreviousContext(const uint64_t user_id)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    auto user_context = this->user_messages->find(user_id)->second.user_chatHistory;
    if (user_context.size() <= this->default_message_line)
    {
        return std::string("没有上下文！");
    }
    // 删除最近的上下文
    user_context.pop_back();
    user_context.pop_back();
    // 跟具体数据同步
    this->user_messages->find(user_id)->second.user_chatHistory = user_context;
    return std::string("上条对话已被删除！");
}
