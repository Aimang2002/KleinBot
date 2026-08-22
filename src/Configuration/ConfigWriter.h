#ifndef CONFIG_WRITER_H
#define CONFIG_WRITER_H

#include "ConfigDiagnostic.h"
#include "../../Library/nlohmann/json.hpp"

#include <mutex>
#include <string>
#include <vector>

// 密钥字段下发到浏览器前统一替换成的哨兵值
inline const std::string kConfigMaskedSentinel = "__KLEIN_MASKED__";

// 递归把键名为 api_key/access_token/secret 的值（明文或 {literal/from_env} 对象）替换为哨兵
nlohmann::json maskSecrets(const nlohmann::json &document);

// 把候选文档中仍为哨兵的值恢复为 current 中的原值；current 缺失的位置保持候选原值
nlohmann::json restoreMaskedSecrets(const nlohmann::json &candidate, const nlohmann::json &current);

// 通用 JSON 原子写：dump(4) + 换行 → 同目录临时文件（自动创建父目录）→ 0600 → rename
// （Windows 上 rename 覆盖失败时回退覆盖复制）；失败时写入 diagnostics 并返回 false
bool writeJsonAtomically(const std::string &path, const nlohmann::json &document,
                         std::vector<ConfigDiagnostic> &diagnostics);

class ConfigWriter
{
public:
    struct Result
    {
        bool success = false;
        // 失败时为拒绝原因；成功时携带校验产生的 Warning/Info 供面板提示
        std::vector<ConfigDiagnostic> diagnostics;
    };

    // 把候选配置写回 path：仍为哨兵的位置从当前文件恢复原值（保留 from_env 形态与未知键），
    // 经 ConfigLoader 全量校验通过后才原子落盘
    Result write(const std::string &path, const nlohmann::json &candidate);

private:
    std::mutex writeMutex;
};

#endif
