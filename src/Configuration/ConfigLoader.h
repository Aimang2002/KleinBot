#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include "AppConfig.h"
#include "ConfigDiagnostic.h"
#include "../../Library/nlohmann/json.hpp"

#include <memory>
#include <string>
#include <vector>

struct ConfigLoadResult
{
    std::shared_ptr<const AppConfig> config;
    std::vector<ConfigDiagnostic> diagnostics;

    bool canStart() const;
};

class ConfigLoader
{
public:
    ConfigLoadResult loadFile(const std::string &path) const;
    ConfigLoadResult loadText(const std::string &content) const;
    ConfigLoadResult loadDocument(const nlohmann::json &document) const;
};

#endif
