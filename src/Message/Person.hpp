#ifndef PERSON_H
#define PERSON_H
#include <iostream>
#include <vector>

// 用户类
class Person
{
public:
    // 基本信息
    std::vector<std::pair<std::string, time_t>> user_chatHistory; // 用户聊天信息和时间戳

    // 模型参数信息
    std::pair<std::string, std::vector<std::string>> user_models; // 用户使用的模型,frist为模型名称，second为api_key、endpoint、API标准
    std::string temperature;                                      // 温度
    std::string top_p;                                            // 顶层P值
    std::string frequency_penalty;                                // 频率惩罚
    std::string presence_penalty;                                 // 存在惩罚

    // 其他信息
    bool isOpenVoiceMode; // 是否开启语音模式，默认为否
};

#endif