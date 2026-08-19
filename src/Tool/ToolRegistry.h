#ifndef TOOL_REGISTRY_H
#define TOOL_REGISTRY_H

#include "Tool.h"
#include "../../Library/nlohmann/json.hpp"
#include <vector>
#include <memory>
#include <string>

class ToolRegistry
{
public:
    void registerTool(std::unique_ptr<Tool> tool)
    {
        tools.push_back(std::move(tool));
    }

    // 返回每个工具的完整 OpenAI tool 定义（JSON 字符串），供 ChatRequest.tools 使用
    std::vector<std::string> allSchemas() const
    {
        std::vector<std::string> result;
        for (const auto &t : tools)
        {
            nlohmann::json tool_def;
            tool_def["type"] = "function";
            tool_def["function"]["name"] = t->name();
            tool_def["function"]["description"] = t->description();
            tool_def["function"]["parameters"] = nlohmann::json::parse(t->parametersSchema());
            result.push_back(tool_def.dump());
        }
        return result;
    }

    // 按名查找，未注册返回 nullptr
    Tool *find(const std::string &name) const
    {
        for (const auto &t : tools)
        {
            if (t->name() == name)
            {
                return t.get();
            }
        }
        return nullptr;
    }

    std::vector<std::string> names() const
    {
        std::vector<std::string> result;
        result.reserve(tools.size());
        for (const auto &tool : tools)
            result.push_back(tool->name());
        return result;
    }

private:
    std::vector<std::unique_ptr<Tool>> tools;
};

#endif // TOOL_REGISTRY_H
