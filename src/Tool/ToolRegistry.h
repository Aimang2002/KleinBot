#ifndef TOOL_REGISTRY_H
#define TOOL_REGISTRY_H

#include "Tool.h"
#include "../../Library/nlohmann/json.hpp"
#include <vector>
#include <memory>
#include <optional>
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
            result.push_back(schemaOf(*t));
        return result;
    }

    // 单个工具的 OpenAI tool 定义（与 allSchemas() 同形）；未注册返回 nullopt。
    // 供只开放部分工具的调用方（如群会话轮）按名挑选
    std::optional<std::string> schemaFor(const std::string &name) const
    {
        const Tool *tool = find(name);
        if (tool == nullptr)
            return std::nullopt;
        return schemaOf(*tool);
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
    static std::string schemaOf(const Tool &tool)
    {
        nlohmann::json tool_def;
        tool_def["type"] = "function";
        tool_def["function"]["name"] = tool.name();
        tool_def["function"]["description"] = tool.description();
        tool_def["function"]["parameters"] = nlohmann::json::parse(tool.parametersSchema());
        return tool_def.dump();
    }

    std::vector<std::unique_ptr<Tool>> tools;
};

#endif // TOOL_REGISTRY_H
