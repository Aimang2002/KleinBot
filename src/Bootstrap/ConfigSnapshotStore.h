#ifndef CONFIG_SNAPSHOT_STORE_H
#define CONFIG_SNAPSHOT_STORE_H

#include "ConfigDiff.h"
#include "RuntimeSettings.h"
#include "../Configuration/ConfigDiagnostic.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct ConfigSnapshot
{
    std::shared_ptr<const SchemaConfig> schema;
    RuntimeSettings runtime;
};

struct ConfigReloadResult
{
    bool success = false;
    ConfigDiff diff;
    std::vector<ConfigDiagnostic> diagnostics;
    std::shared_ptr<const ConfigSnapshot> snapshot;
};

class ConfigSnapshotStore
{
public:
    ConfigSnapshotStore(std::string path, std::shared_ptr<const SchemaConfig> initialSchema);

    std::shared_ptr<const ConfigSnapshot> current() const;
    ConfigReloadResult reload();

private:
    std::string path;
    std::mutex reloadMutex;
    mutable std::mutex mutex;
    std::shared_ptr<const ConfigSnapshot> activeSnapshot;
};

#endif
