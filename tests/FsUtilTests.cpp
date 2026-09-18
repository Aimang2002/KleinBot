#include <gtest/gtest.h>

#include "utils/FsUtil.h"

#include <filesystem>
#include <string>

namespace
{
class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/kleinbot-fsutil-tests-XXXXXX";
        pattern.push_back('\0');
        char *created = mkdtemp(pattern.data());
        if (created != nullptr)
            directory = created;
    }

    ~TemporaryDirectory()
    {
        if (!directory.empty())
            std::filesystem::remove_all(directory);
    }

    const std::string &path() const { return directory; }

private:
    std::string directory;
};
}

TEST(FsUtilTest, CreatesMissingNestedParentDirectories)
{
    TemporaryDirectory directory;
    ASSERT_FALSE(directory.path().empty());
    const std::string filePath = directory.path() + "/fresh/deploy/db.sqlite";
    utils::ensureParentDirectories(filePath);
    EXPECT_TRUE(std::filesystem::is_directory(directory.path() + "/fresh/deploy"));
}

TEST(FsUtilTest, BareFilenameWithoutParentIsNoOp)
{
    // 无父目录部分：目标是工作目录下的相对路径，不应创建任何东西也不应抛异常
    utils::ensureParentDirectories("kleinbot-fsutil-bare.sqlite");
    SUCCEED();
}

TEST(FsUtilTest, ExistingParentDirectoryIsKeptIntact)
{
    TemporaryDirectory directory;
    ASSERT_FALSE(directory.path().empty());
    const std::string nested = directory.path() + "/existing";
    std::filesystem::create_directories(nested);
    utils::ensureParentDirectories(nested + "/db.sqlite");
    EXPECT_TRUE(std::filesystem::is_directory(nested));
}
