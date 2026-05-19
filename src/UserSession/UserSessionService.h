#ifndef USERSESSIONSERVICE_H
#define USERSESSIONSERVICE_H

#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "../Message/Person.hpp"

class UserSessionService
{
public:
    UserSessionService();
    void ensureUserExists(const uint64_t user_id);
    void resetChat(const uint64_t user_id);
    std::string getModelName(uint64_t user_id);
    void setPersonality(const uint64_t user_id, const std::string &Personality);
    void switchModel(const uint64_t user_id, const std::pair<std::string, std::vector<std::string>> &modelName);
    void voiceSwtich(const uint64_t user_id, const bool tag);
    bool isVoiceModel(const uint64_t user_id);
    std::string removePreviousContext(const uint64_t user_id);

private:
    std::string users_message_format = R"({"role": "user", "content": ")";
    std::string bot_message_format = R"({"role": "assistant", "content": ")";
    std::string system_message_format = R"({"system": "user", "content": ")";
    std::mutex mutex_message;
    std::string default_personality;
    short default_message_line = 2;
    std::unique_ptr<std::unordered_map<uint64_t, Person>> user_messages; // key = QQ,second = 用户信息

private:
    void ensureUserExistsUnlock(const uint64_t user_id);
    Person createDefaultPerson(const uint64_t user_id);
};

#endif // USERSESSIONSERVICE_H