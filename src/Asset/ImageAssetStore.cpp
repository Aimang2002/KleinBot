#include "ImageAssetStore.h"
#include "../Log/Log.h"
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

std::string makeAssetId()
{
    std::random_device randomSource;
    std::uniform_int_distribution<uint32_t> distribution;
    std::ostringstream stream;
    stream << "img_" << std::hex << std::setfill('0');
    for (int index = 0; index < 4; ++index)
        stream << std::setw(8) << distribution(randomSource);
    return stream.str();
}

bool assetIdExists(sqlite3 *db, const std::string &assetId)
{
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM image_assets WHERE asset_id=? LIMIT 1;",
                           -1, &statement, nullptr) != SQLITE_OK)
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

ImageAssetStore::ImageAssetStore(const std::string &dbPath, const std::string &directory)
    : assetDirectory(directory)
{
    std::filesystem::create_directories(assetDirectory);
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK)
    {
        LOG_ERROR("图片资源 SQLite 打开失败：" + std::string(sqlite3_errmsg(db)));
        sqlite3_close(db);
        db = nullptr;
        return;
    }
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    const char *ddl =
        "CREATE TABLE IF NOT EXISTS image_assets ("
        " asset_id TEXT PRIMARY KEY,"
        " user_id INTEGER NOT NULL,"
        " source TEXT NOT NULL,"
        " local_path TEXT NOT NULL,"
        " mime_type TEXT NOT NULL,"
        " prompt TEXT NOT NULL DEFAULT '',"
        " source_message_id INTEGER NOT NULL DEFAULT 0,"
        " created_at INTEGER NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_image_assets_user_time "
        "ON image_assets(user_id, created_at DESC);";
    char *error = nullptr;
    if (sqlite3_exec(db, ddl, nullptr, nullptr, &error) != SQLITE_OK)
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
                     "inbound", "", source_message_id);
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
        asset.asset_id = makeAssetId();
        asset.local_path = std::filesystem::absolute(
                               std::filesystem::path(assetDirectory) /
                               (asset.asset_id + extensionForMime(mime_type)))
                               .string();
        if (!assetIdExists(db, asset.asset_id) && !std::filesystem::exists(asset.local_path))
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

std::optional<ImageAsset> ImageAssetStore::insertAsset(const ImageAsset &asset)
{
    const char *sql = "INSERT INTO image_assets "
                      "(asset_id,user_id,source,local_path,mime_type,prompt,source_message_id,created_at) "
                      "VALUES (?,?,?,?,?,?,?,?);";
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
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
    auto assets = query("SELECT asset_id,user_id,source,local_path,mime_type,prompt,source_message_id,created_at "
                        "FROM image_assets WHERE user_id=? AND asset_id=? LIMIT 1;",
                        user_id, asset_id, "", 1);
    return assets.empty() ? std::nullopt : std::optional<ImageAsset>(assets.front());
}

std::optional<ImageAsset> ImageAssetStore::findLatest(uint64_t user_id, const std::string &source) const
{
    auto assets = query("SELECT asset_id,user_id,source,local_path,mime_type,prompt,source_message_id,created_at "
                        "FROM image_assets WHERE user_id=? AND source=? "
                        "ORDER BY created_at DESC, rowid DESC LIMIT 1;",
                        user_id, "", source, 1);
    return assets.empty() ? std::nullopt : std::optional<ImageAsset>(assets.front());
}

std::vector<ImageAsset> ImageAssetStore::list(uint64_t user_id, const std::string &source, int limit) const
{
    return query("SELECT asset_id,user_id,source,local_path,mime_type,prompt,source_message_id,created_at "
                 "FROM image_assets WHERE user_id=? AND source=? "
                 "ORDER BY created_at DESC, rowid DESC LIMIT ?;",
                 user_id, "", source, limit);
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
    const char *sql = "UPDATE image_assets SET source_message_id=? WHERE user_id=? AND asset_id=?;";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
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
    if (sqlite3_prepare_v2(db, "SELECT asset_id,user_id,source,local_path,mime_type,prompt,source_message_id,created_at FROM image_assets WHERE user_id=?;", -1, &select, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(select, 1, static_cast<sqlite3_int64>(user_id));
        while (sqlite3_step(select) == SQLITE_ROW)
            assets.push_back(readAsset(select));
    }
    sqlite3_finalize(select);
    sqlite3_stmt *deleteStatement = nullptr;
    if (sqlite3_prepare_v2(db, "DELETE FROM image_assets WHERE user_id=?;", -1, &deleteStatement, nullptr) == SQLITE_OK)
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
    const char *selectSql = "SELECT asset_id,user_id,source,local_path,mime_type,prompt,source_message_id,created_at "
                            "FROM image_assets WHERE user_id=? AND source_message_id>=?;";
    if (sqlite3_prepare_v2(db, selectSql, -1, &select, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(select, 1, static_cast<sqlite3_int64>(user_id));
        sqlite3_bind_int64(select, 2, first_message_id);
        while (sqlite3_step(select) == SQLITE_ROW)
            assets.push_back(readAsset(select));
    }
    sqlite3_finalize(select);
    sqlite3_stmt *deleteStatement = nullptr;
    const char *deleteSql = "DELETE FROM image_assets WHERE user_id=? AND source_message_id>=?;";
    if (sqlite3_prepare_v2(db, deleteSql, -1, &deleteStatement, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(deleteStatement, 1, static_cast<sqlite3_int64>(user_id));
        sqlite3_bind_int64(deleteStatement, 2, first_message_id);
        sqlite3_step(deleteStatement);
    }
    sqlite3_finalize(deleteStatement);
    removeFiles(assets);
}
