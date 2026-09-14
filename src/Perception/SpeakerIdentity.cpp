#include "SpeakerIdentity.h"
#include "../utils/Hash.h"

#include <sqlite3.h>

#include <mutex>
#include <random>

namespace
{
std::string toHex(std::uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index)
    {
        result[static_cast<std::size_t>(index)] = digits[value & 0xF];
        value >>= 4;
    }
    return result;
}

std::string generateSalt()
{
    static std::mutex mutex;
    static std::mt19937_64 engine(std::random_device{}());
    static std::uniform_int_distribution<std::uint64_t> value;
    std::lock_guard<std::mutex> lock(mutex);
    return toHex(value(engine)) + toHex(value(engine));
}
}

namespace perception
{
std::string loadOrCreateSpeakerSalt(sqlite3 *db)
{
    if (db == nullptr)
        return {};

    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db,
                           "SELECT value FROM perception_meta WHERE key='speaker_salt';",
                           -1, &statement, nullptr) == SQLITE_OK)
    {
        if (sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_text(statement, 0) != nullptr)
        {
            const std::string salt = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
            sqlite3_finalize(statement);
            if (!salt.empty())
                return salt;
        }
        else
        {
            sqlite3_finalize(statement);
        }
    }

    const std::string salt = generateSalt();
    sqlite3_stmt *insert = nullptr;
    if (sqlite3_prepare_v2(db,
                           "INSERT OR REPLACE INTO perception_meta(key, value)"
                           " VALUES ('speaker_salt', ?1);",
                           -1, &insert, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(insert, 1, salt.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(insert);
        sqlite3_finalize(insert);
    }
    return salt;
}

std::string speakerIdOf(const std::string &salt, std::uint64_t qq)
{
    if (salt.empty())
        return {};
    return utils::hmacSha1Hex(salt, std::to_string(qq));
}
}
