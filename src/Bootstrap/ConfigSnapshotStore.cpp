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
    result.diagnostics = std::move(loaded.diagnostics);
    if (!loaded.canStart())
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
