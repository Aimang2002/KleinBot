#include "ConfigSnapshotStore.h"

#include "../Configuration/ConfigLoader.h"

#include <utility>

ConfigSnapshotStore::ConfigSnapshotStore(
    std::string path, std::shared_ptr<const SchemaConfig> initialSchema)
    : path(std::move(path))
{
    auto initial = std::make_shared<ConfigSnapshot>();
    initial->schema = std::move(initialSchema);
    initial->runtime = buildRuntimeSettings(*initial->schema);
    activeSnapshot = std::move(initial);
}

std::shared_ptr<const ConfigSnapshot> ConfigSnapshotStore::current() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return activeSnapshot;
}

ConfigReloadResult ConfigSnapshotStore::reload()
{
    std::lock_guard<std::mutex> reloadLock(reloadMutex);
    ConfigLoader loader;
    ConfigLoadResult loaded = loader.loadFile(path);

    ConfigReloadResult result;
    // canStart() 要看诊断里有没有 Error/Fatal：必须在诊断移进结果之前判断，
    // 否则能解析但不合格的候选也会被发布
    const bool startable = loaded.canStart();
    result.diagnostics = std::move(loaded.diagnostics);
    if (!startable)
    {
        result.snapshot = current();
        return result;
    }

    auto candidate = std::make_shared<ConfigSnapshot>();
    candidate->schema = std::move(loaded.config);
    candidate->runtime = buildRuntimeSettings(*candidate->schema);

    {
        std::lock_guard<std::mutex> lock(mutex);
        result.diff = compareConfig(*activeSnapshot->schema, *candidate->schema);
        activeSnapshot = candidate;
        result.snapshot = activeSnapshot;
    }
    result.success = true;
    return result;
}
