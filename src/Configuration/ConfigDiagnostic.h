#ifndef CONFIG_DIAGNOSTIC_H
#define CONFIG_DIAGNOSTIC_H

#include <string>

enum class ConfigSeverity
{
    Info,
    Warning,
    FeatureDisabled,
    Error,
    Fatal
};

enum class ConfigErrorCategory
{
    Source,
    Syntax,
    Version,
    Missing,
    Type,
    Range,
    Unknown,
    Security,
    Dependency
};

struct ConfigDiagnostic
{
    ConfigSeverity severity;
    ConfigErrorCategory category;
    std::string path;
    std::string message;
};

std::string configSeverityName(ConfigSeverity severity);

#endif
