#include "ImageAssetStore.h"
#include "../Log/Log.h"
#include "../utils/FsUtil.h"
#include <curl/curl.h>
#include <sqlite3.h>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace
{
size_t writeBytes(void *ptr, size_t size, size_t count, void *userdata)
{
    auto *data = static_cast<std::string *>(userdata);
    data->append(static_cast<const char *>(ptr), size * count);
    return size * count;
}

std::string base64Decode(std::string input)
{
    const std::string alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    int value = 0;
    int bits = -8;
    for (unsigned char character : input)
    {
        if (character == '=')
            break;
        const auto position = alphabet.find(character);
        if (position == std::string::npos)
            continue;
        value = (value << 6) + static_cast<int>(position);
        bits += 6;
        if (bits >= 0)
        {
            output.push_back(static_cast<char>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return output;
}

std::string extensionForMime(const std::string &mime)
{
    if (mime.find("png") != std::string::npos)
        return ".png";
    if (mime.find("webp") != std::string::npos)
        return ".webp";
    if (mime.find("gif") != std::string::npos)
        return ".gif";
    return ".jpg";
}

std::string makeAssetId(const std::string &prefix)
{
    std::random_device randomSource;
    std::uniform_int_distribution<uint32_t> distribution;
    std::ostringstream stream;
    stream << prefix << std::hex << std::setfill('0');
    for (int index = 0; index < 4; ++index)
        stream << std::setw(8) << distribution(randomSource);
    return stream.str();
}

bool assetIdExists(sqlite3 *db, const std::string &table, const std::string &assetId)
{
    sqlite3_stmt *statement = nullptr;
    const std::string sql = "SELECT 1 FROM " + table + " WHERE asset_id=? LIMIT 1;";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
        return true;
    sqlite3_bind_text(statement, 1, assetId.c_str(), -1, SQLITE_TRANSIENT);
    const bool exists = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return exists;
}

std::string columnText(sqlite3_stmt *statement, int column)
{
    const auto *value = sqlite3_column_text(statement, column);
    return value == nullptr ? std::string() : std::string(reinterpret_cast<const char *>(value));
}

ImageAsset readAsset(sqlite3_stmt *statement)
{
    ImageAsset asset;
    asset.asset_id = columnText(statement, 0);
    asset.user_id = static_cast<uint64_t>(sqlite3_column_int64(statement, 1));
    asset.source = columnText(statement, 2);
    asset.local_path = columnText(statement, 3);
    asset.mime_type = columnText(statement, 4);
    asset.prompt = columnText(statement, 5);
    asset.source_message_id = sqlite3_column_int64(statement, 6);
    asset.created_at = sqlite3_column_int64(statement, 7);
    return asset;
}
}

ImageAssetStore::ImageAssetStore(const std::string &dbPath, const std::string &directory,
                                 std::string tableName, std::string prefix)
    : assetDirectory(directory), table(std::move(tableName)), idPrefix(std::move(prefix))
{
    std::filesystem::create_directories(assetDirectory);
    utils::ensureParentDirectories(dbPath);
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK)
    {
        LOG_ERROR("图片资源 SQLite 打开失败：" + std::string(sqlite3_errmsg(db)));
        sqlite3_close(db);
        db = nullptr;
        return;
    }
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    const std::string ddl =
        "CREATE TABLE IF NOT EXISTS " + table + " ("
        " asset_id TEXT PRIMARY KEY,"
        " user_id INTEGER NOT NULL,"
        " source TEXT NOT NULL,"
        " local_path TEXT NOT NULL,"
        " mime_type TEXT NOT NULL,"
        " prompt TEXT NOT NULL DEFAULT '',"
        " source_message_id INTEGER NOT NULL DEFAULT 0,"
        " created_at INTEGER NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_" + table + "_user_time "
        "ON " + table + "(user_id, created_at DESC);";
    char *error = nullptr;
    if (sqlite3_exec(db, ddl.c_str(), nullptr, nullptr, &error) != SQLITE_OK)
    {
        LOG_ERROR("图片资源建表失败：" + std::string(error == nullptr ? "?" : error));
        sqlite3_free(error);
        sqlite3_close(db);
        db = nullptr;
    }
}

ImageAssetStore::~ImageAssetStore()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (db != nullptr)
        sqlite3_close(db);
}

std::optional<ImageAsset> ImageAssetStore::importFromUrl(uint64_t user_id, const std::string &url,
                                                         int64_t source_message_id)
{
    return importFromUrl(user_id, url, "inbound", "", source_message_id);
}

std::optional<ImageAsset> ImageAssetStore::importFromUrl(uint64_t user_id, const std::string &url,
                                                         const std::string &source,
                                                         const std::string &prompt,
                                                         int64_t source_message_id)
{
    if (url.empty())
        return std::nullopt;

    CURL *curl = curl_easy_init();
    if (curl == nullptr)
        return std::nullopt;

    std::string bytes;
    std::string contentType;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBytes);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bytes);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION,
                     +[](char *buffer, size_t size, size_t count, void *userdata) -> size_t {
                         auto *type = static_cast<std::string *>(userdata);
                         std::string line(buffer, size * count);
                         const std::string prefix = "Content-Type:";
                         if (line.rfind(prefix, 0) == 0)
                         {
                             *type = line.substr(prefix.size());
                             while (!type->empty() && std::isspace(type->front()))
                                 type->erase(type->begin());
                             while (!type->empty() && std::isspace(type->back()))
                                 type->pop_back();
                         }
                         return size * count;
                     });
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &contentType);
    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK || status >= 400 || bytes.empty())
    {
        LOG_ERROR("图片下载失败：" + std::string(curl_easy_strerror(result)));
        return std::nullopt;
    }
    if (bytes.size() > 20 * 1024 * 1024)
    {
        LOG_ERROR("图片超过20MB限制，拒绝保存");
        return std::nullopt;
    }
    return saveBytes(user_id, bytes, contentType.empty() ? "image/jpeg" : contentType,
                     source, prompt, source_message_id);
}

std::optional<ImageAsset> ImageAssetStore::saveBase64(uint64_t user_id, const std::string &base64,
                                                      const std::string &source,
                                                      const std::string &prompt,
                                                      int64_t source_message_id)
{
    if (base64.empty())
        return std::nullopt;
    return saveBytes(user_id, base64Decode(base64), "image/png", source, prompt, source_message_id);
}

std::optional<ImageAsset> ImageAssetStore::saveBytes(uint64_t user_id, const std::string &bytes,
                                                    const std::string &mime_type,
                                                    const std::string &source,
                                                    const std::string &prompt,
                                                    int64_t source_message_id)
{
    if (bytes.empty())
        return std::nullopt;
    if (bytes.size() > 20 * 1024 * 1024)
    {
        LOG_ERROR("图片超过20MB限制，拒绝保存");
        return std::nullopt;
    }
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr)
        return std::nullopt;

    ImageAsset asset;
    asset.user_id = user_id;
    asset.source = source;
    asset.mime_type = mime_type;
    asset.prompt = prompt;
    asset.source_message_id = source_message_id;
    asset.created_at = static_cast<int64_t>(std::time(nullptr));

    constexpr int maxIdAttempts = 16;
    for (int attempt = 0; attempt < maxIdAttempts; ++attempt)
    {
        asset.asset_id = makeAssetId(idPrefix);
        asset.local_path = std::filesystem::absolute(
                               std::filesystem::path(assetDirectory) /
                               (asset.asset_id + extensionForMime(mime_type)))
                               .string();
        if (!assetIdExists(db, table, asset.asset_id) && !std::filesystem::exists(asset.local_path))
            break;
        asset.asset_id.clear();
        asset.local_path.clear();
    }
    if (asset.asset_id.empty())
    {
        LOG_ERROR("图片资源 ID 连续碰撞，拒绝保存");
        return std::nullopt;
    }

    std::ofstream file(asset.local_path, std::ios::binary);
    if (!file.is_open())
    {
        LOG_ERROR("图片资源文件创建失败：" + asset.local_path);
        return std::nullopt;
    }
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();

    auto inserted = insertAsset(asset);
    if (!inserted)
        std::filesystem::remove(asset.local_path);
    return inserted;
}

std::string ImageAssetStore::selectPrefix() const
{
    return "SELECT asset_id,user_id,source,local_path,mime_type,prompt,source_message_id,created_at "
           "FROM " + table + " ";
}

std::optional<ImageAsset> ImageAssetStore::insertAsset(const ImageAsset &asset)
{
    const std::string sql = "INSERT INTO " + table + " "
                            "(asset_id,user_id,source,local_path,mime_type,prompt,source_message_id,created_at) "
                            "VALUES (?,?,?,?,?,?,?,?);";
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
        return std::nullopt;
    sqlite3_bind_text(statement, 1, asset.asset_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(asset.user_id));
    sqlite3_bind_text(statement, 3, asset.source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, asset.local_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, asset.mime_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 6, asset.prompt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 7, asset.source_message_id);
    sqlite3_bind_int64(statement, 8, asset.created_at);
    const bool success = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return success ? std::optional<ImageAsset>(asset) : std::nullopt;
}

std::vector<ImageAsset> ImageAssetStore::query(const std::string &sql, uint64_t user_id,
                                               const std::string &asset_id,
                                               const std::string &source, int limit) const
{
    std::vector<ImageAsset> result;
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr)
        return result;
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
        return result;
    int index = 1;
    sqlite3_bind_int64(statement, index++, static_cast<sqlite3_int64>(user_id));
    if (sql.find("asset_id=?") != std::string::npos)
        sqlite3_bind_text(statement, index++, asset_id.c_str(), -1, SQLITE_TRANSIENT);
    if (sql.find("source=?") != std::string::npos)
        sqlite3_bind_text(statement, index++, source.c_str(), -1, SQLITE_TRANSIENT);
    if (sql.find("LIMIT ?") != std::string::npos)
        sqlite3_bind_int(statement, index, limit);
    while (sqlite3_step(statement) == SQLITE_ROW)
        result.push_back(readAsset(statement));
    sqlite3_finalize(statement);
    return result;
}

std::optional<ImageAsset> ImageAssetStore::find(uint64_t user_id, const std::string &asset_id) const
{
    auto assets = query(selectPrefix() + "WHERE user_id=? AND asset_id=? LIMIT 1;",
                        user_id, asset_id, "", 1);
    return assets.empty() ? std::nullopt : std::optional<ImageAsset>(assets.front());
}

std::optional<ImageAsset> ImageAssetStore::findLatest(uint64_t user_id, const std::string &source) const
{
    auto assets = query(selectPrefix() + "WHERE user_id=? AND source=? "
                        "ORDER BY created_at DESC, rowid DESC LIMIT 1;",
                        user_id, "", source, 1);
    return assets.empty() ? std::nullopt : std::optional<ImageAsset>(assets.front());
}

std::optional<ImageAsset> ImageAssetStore::findLatest(uint64_t user_id) const
{
    auto assets = query(selectPrefix() + "WHERE user_id=? ORDER BY created_at DESC, rowid DESC LIMIT 1;",
                        user_id, "", "", 1);
    return assets.empty() ? std::nullopt : std::optional<ImageAsset>(assets.front());
}

std::optional<ImageAsset> ImageAssetStore::findBySourceMessage(uint64_t user_id,
                                                               int64_t source_message_id) const
{
    if (source_message_id == 0)
        return std::nullopt;
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr)
        return std::nullopt;
    const std::string sql = selectPrefix() + "WHERE user_id=? AND source_message_id=? "
                            "ORDER BY created_at DESC, rowid DESC LIMIT 1;";
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
        return std::nullopt;
    sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(user_id));
    sqlite3_bind_int64(statement, 2, source_message_id);
    std::optional<ImageAsset> result;
    if (sqlite3_step(statement) == SQLITE_ROW)
        result = readAsset(statement);
    sqlite3_finalize(statement);
    return result;
}

std::optional<ImageAsset> ImageAssetStore::findByLocalPath(uint64_t user_id,
                                                           const std::string &local_path) const
{
    if (local_path.empty())
        return std::nullopt;
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr)
        return std::nullopt;
    const std::string sql = selectPrefix() + "WHERE user_id=? AND local_path=? LIMIT 1;";
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
        return std::nullopt;
    sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(user_id));
    sqlite3_bind_text(statement, 2, local_path.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<ImageAsset> result;
    if (sqlite3_step(statement) == SQLITE_ROW)
        result = readAsset(statement);
    sqlite3_finalize(statement);
    return result;
}

std::vector<ImageAsset> ImageAssetStore::list(uint64_t user_id, const std::string &source, int limit) const
{
    return query(selectPrefix() + "WHERE user_id=? AND source=? "
                 "ORDER BY created_at DESC, rowid DESC LIMIT ?;",
                 user_id, "", source, limit);
}

std::size_t ImageAssetStore::prune(int64_t now, int64_t ttlSeconds, int perOwnerCap)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr)
        return 0;
    std::vector<ImageAsset> expired;
    // TTL 到期的，以及每个 owner 按 created_at 倒序排在 perOwnerCap 之后的
    const std::string sql = selectPrefix() +
        "WHERE created_at < ?1 OR asset_id IN ("
        "  SELECT a.asset_id FROM " + table + " a"
        "  WHERE (SELECT COUNT(*) FROM " + table + " b WHERE b.user_id=a.user_id"
        "         AND (b.created_at > a.created_at OR (b.created_at = a.created_at AND b.rowid > a.rowid))) >= ?2);";
    sqlite3_stmt *select = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &select, nullptr) != SQLITE_OK)
        return 0;
    sqlite3_bind_int64(select, 1, now - ttlSeconds);
    sqlite3_bind_int(select, 2, perOwnerCap);
    while (sqlite3_step(select) == SQLITE_ROW)
        expired.push_back(readAsset(select));
    sqlite3_finalize(select);
    if (expired.empty())
        return 0;

    const std::string deleteSql = "DELETE FROM " + table + " WHERE asset_id=?;";
    for (const ImageAsset &asset : expired)
    {
        sqlite3_stmt *statement = nullptr;
        if (sqlite3_prepare_v2(db, deleteSql.c_str(), -1, &statement, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(statement, 1, asset.asset_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(statement);
            sqlite3_finalize(statement);
        }
    }
    removeFiles(expired);
    return expired.size();
}

std::string ImageAssetStore::readBase64(const ImageAsset &asset) const
{
    std::ifstream file(asset.local_path, std::ios::binary);
    if (!file.is_open())
        return {};
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::string alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int value = 0;
    int bits = -6;
    for (unsigned char character : bytes)
    {
        value = (value << 8) + character;
        bits += 8;
        while (bits >= 0)
        {
            result.push_back(alphabet[(value >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6)
        result.push_back(alphabet[((value << 8) >> (bits + 8)) & 0x3F]);
    while (result.size() % 4 != 0)
        result.push_back('=');
    return result;
}

void ImageAssetStore::attachToConversation(uint64_t user_id, const std::string &asset_id,
                                           int64_t message_id)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr || asset_id.empty() || message_id <= 0)
        return;
    sqlite3_stmt *statement = nullptr;
    const std::string sql = "UPDATE " + table + " SET source_message_id=? WHERE user_id=? AND asset_id=?;";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
        return;
    sqlite3_bind_int64(statement, 1, message_id);
    sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(user_id));
    sqlite3_bind_text(statement, 3, asset_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(statement);
    sqlite3_finalize(statement);
}

void ImageAssetStore::removeFiles(const std::vector<ImageAsset> &assets) const
{
    for (const auto &asset : assets)
    {
        std::error_code error;
        std::filesystem::remove(asset.local_path, error);
    }
}

void ImageAssetStore::clearUser(uint64_t user_id)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr)
        return;
    std::vector<ImageAsset> assets;
    sqlite3_stmt *select = nullptr;
    const std::string selectSql = selectPrefix() + "WHERE user_id=?;";
    if (sqlite3_prepare_v2(db, selectSql.c_str(), -1, &select, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(select, 1, static_cast<sqlite3_int64>(user_id));
        while (sqlite3_step(select) == SQLITE_ROW)
            assets.push_back(readAsset(select));
    }
    sqlite3_finalize(select);
    sqlite3_stmt *deleteStatement = nullptr;
    const std::string deleteSql = "DELETE FROM " + table + " WHERE user_id=?;";
    if (sqlite3_prepare_v2(db, deleteSql.c_str(), -1, &deleteStatement, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(deleteStatement, 1, static_cast<sqlite3_int64>(user_id));
        sqlite3_step(deleteStatement);
    }
    sqlite3_finalize(deleteStatement);
    removeFiles(assets);
}

void ImageAssetStore::removeByConversationFrom(uint64_t user_id, int64_t first_message_id)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (db == nullptr || first_message_id <= 0)
        return;
    std::vector<ImageAsset> assets;
    sqlite3_stmt *select = nullptr;
    const std::string selectSql = selectPrefix() + "WHERE user_id=? AND source_message_id>=?;";
    if (sqlite3_prepare_v2(db, selectSql.c_str(), -1, &select, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(select, 1, static_cast<sqlite3_int64>(user_id));
        sqlite3_bind_int64(select, 2, first_message_id);
        while (sqlite3_step(select) == SQLITE_ROW)
            assets.push_back(readAsset(select));
    }
    sqlite3_finalize(select);
    sqlite3_stmt *deleteStatement = nullptr;
    const std::string deleteSql = "DELETE FROM " + table + " WHERE user_id=? AND source_message_id>=?;";
    if (sqlite3_prepare_v2(db, deleteSql.c_str(), -1, &deleteStatement, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(deleteStatement, 1, static_cast<sqlite3_int64>(user_id));
        sqlite3_bind_int64(deleteStatement, 2, first_message_id);
        sqlite3_step(deleteStatement);
    }
    sqlite3_finalize(deleteStatement);
    removeFiles(assets);
}
