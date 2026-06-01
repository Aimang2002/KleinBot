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
    void resetPersonality(const uint64_t user_id);
    void switchModel(const uint64_t user_id, const std::pair<std::string, std::vector<std::string>> &modelName);
    void voiceSwitch(const uint64_t user_id, const bool tag);
    bool isVoiceMode(const uint64_t user_id);
    std::string removePreviousContext(const uint64_t user_id);
    std::vector<std::pair<std::string, time_t>> getChatHistory(const uint64_t user_id);
    void updateChatHistory(const uint64_t user_id, const std::vector<std::pair<std::string, time_t>> &history);
    Person getUserConfig(const uint64_t user_id);
    std::string dumpUserMessage(const std::string &content);
    std::string dumpBotMessage(const std::string &content);
    std::string dumpSystemMessage(const std::string &content);
    int getDefaultLine();

private:
    std::mutex mutex_message;
    std::string default_personality;
    short default_message_line = 2;
    std::unique_ptr<std::unordered_map<uint64_t, Person>> user_messages; // key = QQ,second = 用户信息

private:
    void ensureUserExistsUnlock(const uint64_t user_id);
    Person createDefaultPerson(const uint64_t user_id);
};

#endif // USERSESSIONSERVICE_H