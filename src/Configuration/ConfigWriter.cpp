#include "ConfigWriter.h"
#include "ConfigLoader.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace
{
using json = nlohmann::json;

bool isSecretKey(const std::string &key)
{
    static const std::set<std::string> keys = {"api_key", "access_token", "secret"};
    return keys.find(key) != keys.end();
}

bool isMaskedSentinel(const json &value)
{
    return value.is_string() && value.get<std::string>() == kConfigMaskedSentinel;
}

json maskValue(const json &value)
{
    if (value.is_object())
    {
        json masked = json::object();
        for (const auto &entry : value.items())
        {
            if (isSecretKey(entry.key()))
                masked[entry.key()] = kConfigMaskedSentinel;
            else
                masked[entry.key()] = maskValue(entry.value());
        }
        return masked;
    }
    if (value.is_array())
    {
        json masked = json::array();
        for (const json &item : value)
            masked.push_back(maskValue(item));
        return masked;
    }
    return value;
}

// 数组元素按身份匹配恢复密钥，避免删除或重排后按位置恢复把 A 的密钥写进 B。
// 有数组字段（如模型注册表的 ModelName）时只认数组字段相等——枚举/布尔这类弱字段
// （APIStandard、enabled）在多个元素间普遍相等，不能当身份；没有数组字段时退回
// 任意非密钥字段相等。匹配不上则保持候选原值，残留哨兵由调用方兜底置空并提示
bool hasArrayField(const json &object)
{
    for (const auto &entry : object.items())
    {
        if (!isSecretKey(entry.key()) && entry.value().is_array())
            return true;
    }
    return false;
}

bool sharesArrayIdentity(const json &left, const json &right)
{
    for (const auto &entry : left.items())
    {
        if (isSecretKey(entry.key()) || !entry.value().is_array())
            continue;
        const auto iterator = right.find(entry.key());
        if (iterator != right.end() && *iterator == entry.value())
            return true;
    }
    return false;
}

bool sharesIdentity(const json &left, const json &right)
{
    if (hasArrayField(left) || hasArrayField(right))
        return sharesArrayIdentity(left, right);
    for (const auto &entry : left.items())
    {
        if (isSecretKey(entry.key()) || entry.value().is_object() || entry.value().is_null() ||
            entry.value().is_array())
            continue;
        const auto iterator = right.find(entry.key());
        if (iterator != right.end() && *iterator == entry.value())
            return true;
    }
    return false;
}

json restoreValue(const json &candidate, const json &current)
{
    if (isMaskedSentinel(candidate))
        return current;
    if (candidate.is_object() && current.is_object())
    {
        json restored = json::object();
        for (const auto &entry : candidate.items())
        {
            if (current.contains(entry.key()))
                restored[entry.key()] = restoreValue(entry.value(), current.at(entry.key()));
            else
                restored[entry.key()] = entry.value();
        }
        return restored;
    }
    if (candidate.is_array() && current.is_array())
    {
        json restored = json::array();
        std::vector<bool> used(current.size(), false);
        for (const json &element : candidate)
        {
            const json *match = nullptr;
            if (element.is_object())
            {
                for (std::size_t index = 0; index < current.size(); ++index)
                {
                    if (used[index] || !current[index].is_object())
                        continue;
                    if (sharesIdentity(element, current[index]))
                    {
                        match = &current[index];
                        used[index] = true;
                        break;
                    }
                }
            }
            restored.push_back(match == nullptr ? element : restoreValue(element, *match));
        }
        return restored;
    }
    return candidate;
}

std::string temporaryPath(const std::string &path)
{
#if defined(_WIN32)
    const long pid = _getpid();
#else
    const long pid = ::getpid();
#endif
    return path + ".tmp-" + std::to_string(pid);
}
}

json maskSecrets(const json &document)
{
    return maskValue(document);
}

json restoreMaskedSecrets(const json &candidate, const json &current)
{
    return restoreValue(candidate, current);
}

bool writeJsonAtomically(const std::string &path, const json &document,
                         std::vector<ConfigDiagnostic> &diagnostics)
{
    namespace fs = std::filesystem;
    const fs::path target(path);
    const fs::path parent = target.parent_path();
    if (!parent.empty())
    {
        std::error_code directoryError;
        fs::create_directories(parent, directoryError);
    }

    const std::string tempPath = temporaryPath(path);
    {
        std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            diagnostics.push_back({ConfigSeverity::Fatal, ConfigErrorCategory::Source,
                                  tempPath, "临时文件无法创建，写回中止"});
            return false;
        }
        output << document.dump(4) << "\n";
        output.flush();
        if (!output.good())
        {
            diagnostics.push_back({ConfigSeverity::Fatal, ConfigErrorCategory::Source,
                                  tempPath, "临时文件写入不完整，写回中止"});
            return false;
        }
    }

    std::error_code permissionError;
    fs::permissions(tempPath, fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, permissionError);
    // 权限收紧失败不阻断写回：部分 Windows 文件系统不支持 POSIX 权限位

    std::error_code renameError;
    fs::rename(tempPath, path, renameError);
    if (renameError)
    {
        // Windows 上目标已存在时 rename 可能失败，回退为覆盖复制
        std::error_code copyError;
        fs::copy_file(tempPath, path, fs::copy_options::overwrite_existing, copyError);
        fs::remove(tempPath);
        if (copyError)
        {
            diagnostics.push_back({ConfigSeverity::Fatal, ConfigErrorCategory::Source,
                                  path, "文件替换失败：" + renameError.message()});
            return false;
        }
    }
    return true;
}

ConfigWriter::Result ConfigWriter::write(const std::string &path, const json &candidate)
{
    std::lock_guard<std::mutex> lock(writeMutex);
    Result result;

    std::ifstream input(path);
    if (!input.is_open())
    {
        result.diagnostics.push_back({ConfigSeverity::Fatal, ConfigErrorCategory::Source,
                                      path, "当前配置文件无法打开，写回中止"});
        return result;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    json current;
    try
    {
        current = json::parse(buffer.str());
    }
    catch (const std::exception &error)
    {
        result.diagnostics.push_back({ConfigSeverity::Fatal, ConfigErrorCategory::Source,
                                      path, "当前配置文件不是有效 JSON：" + std::string(error.what())});
        return result;
    }

    const json merged = restoreMaskedSecrets(candidate, current);

    const ConfigLoadResult validation = ConfigLoader().loadDocument(merged);
    if (!validation.canStart())
    {
        result.diagnostics = validation.diagnostics;
        return result;
    }

    if (!writeJsonAtomically(path, merged, result.diagnostics))
        return result;

    result.success = true;
    result.diagnostics = validation.diagnostics;
    return result;
}
