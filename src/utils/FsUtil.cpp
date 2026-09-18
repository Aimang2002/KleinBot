#include "FsUtil.h"

#include <filesystem>

namespace utils
{
    void ensureParentDirectories(const std::string &filePath)
    {
        std::error_code error;
        const std::filesystem::path parent = std::filesystem::path(filePath).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent, error);
    }
}
