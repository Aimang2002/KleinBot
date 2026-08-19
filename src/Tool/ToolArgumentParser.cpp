#include "ToolArgumentParser.h"
#include <cctype>
#include <stdexcept>
#include <vector>

namespace
{
std::vector<std::string> splitConcatenatedObjects(const std::string &value)
{
    std::vector<std::string> objects;
    std::size_t position = 0;
    while (position < value.size())
    {
        while (position < value.size() &&
               std::isspace(static_cast<unsigned char>(value[position])))
            ++position;
        if (position == value.size())
            break;
        if (value[position] != '{')
            throw std::invalid_argument("工具参数包含无法识别的非对象内容");

        const std::size_t start = position;
        int depth = 0;
        bool inString = false;
        bool escaped = false;
        for (; position < value.size(); ++position)
        {
            const char character = value[position];
            if (inString)
            {
                if (escaped)
                {
                    escaped = false;
                }
                else if (character == '\\')
                {
                    escaped = true;
                }
                else if (character == '"')
                {
                    inString = false;
                }
                continue;
            }

            if (character == '"')
            {
                inString = true;
            }
            else if (character == '{')
            {
                ++depth;
            }
            else if (character == '}')
            {
                --depth;
                if (depth == 0)
                {
                    objects.push_back(value.substr(start, position - start + 1));
                    ++position;
                    break;
                }
                if (depth < 0)
                    throw std::invalid_argument("工具参数对象括号不匹配");
            }
        }
        if (depth != 0 || inString)
            throw std::invalid_argument("工具参数对象不完整");
    }
    return objects;
}
}

nlohmann::json parseToolArguments(const std::string &arguments)
{
    try
    {
        return nlohmann::json::parse(arguments);
    }
    catch (const nlohmann::json::parse_error &originalError)
    {
        const std::vector<std::string> objects = splitConcatenatedObjects(arguments);
        if (objects.size() < 2)
            throw originalError;

        nlohmann::json merged = nlohmann::json::object();
        for (const std::string &objectText : objects)
        {
            const nlohmann::json object = nlohmann::json::parse(objectText);
            if (!object.is_object())
                throw std::invalid_argument("工具参数必须是 JSON 对象");
            merged.update(object);
        }
        return merged;
    }
}

std::string normalizeToolArguments(const std::string &arguments)
{
    try
    {
        return parseToolArguments(arguments).dump();
    }
    catch (const std::exception &)
    {
        return arguments;
    }
}
