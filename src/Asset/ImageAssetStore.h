#ifndef IMAGE_ASSET_STORE_H
#define IMAGE_ASSET_STORE_H

#include "ImageAsset.h"
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

class ImageAssetStore
{
public:
    ImageAssetStore(const std::string &dbPath, const std::string &assetDirectory);
    ~ImageAssetStore();

    ImageAssetStore(const ImageAssetStore &) = delete;
    ImageAssetStore &operator=(const ImageAssetStore &) = delete;

    std::optional<ImageAsset> importFromUrl(uint64_t user_id, const std::string &url,
                                            int64_t source_message_id);
    std::optional<ImageAsset> saveBase64(uint64_t user_id, const std::string &base64,
                                         const std::string &source, const std::string &prompt,
                                         int64_t source_message_id);
    std::optional<ImageAsset> find(uint64_t user_id, const std::string &asset_id) const;
    std::optional<ImageAsset> findLatest(uint64_t user_id, const std::string &source) const;
    std::vector<ImageAsset> list(uint64_t user_id, const std::string &source, int limit) const;
    std::string readBase64(const ImageAsset &asset) const;
    void attachToConversation(uint64_t user_id, const std::string &asset_id, int64_t message_id);

    void clearUser(uint64_t user_id);
    void removeByConversationFrom(uint64_t user_id, int64_t first_message_id);

private:
    std::optional<ImageAsset> saveBytes(uint64_t user_id, const std::string &bytes,
                                        const std::string &mime_type, const std::string &source,
                                        const std::string &prompt, int64_t source_message_id);
    std::optional<ImageAsset> insertAsset(const ImageAsset &asset);
    std::vector<ImageAsset> query(const std::string &sql, uint64_t user_id,
                                  const std::string &asset_id, const std::string &source,
                                  int limit) const;
    void removeFiles(const std::vector<ImageAsset> &assets) const;

    sqlite3 *db = nullptr;
    std::string assetDirectory;
    mutable std::mutex mutex;
};

#endif // IMAGE_ASSET_STORE_H
