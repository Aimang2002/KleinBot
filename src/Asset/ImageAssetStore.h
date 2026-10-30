#ifndef IMAGE_ASSET_STORE_H
#define IMAGE_ASSET_STORE_H

#include "ImageAsset.h"
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

// 图片资产库。同一份实现跑两个实例（ADR-019，2026-10-28）：
//   - 个人库：表 image_assets，owner = user_id，永久（#重置上下文 清）
//   - 群库：  表 group_image_assets，owner = group_id，id 前缀 gimg_，
//            与群内容缓冲同生命周期（prune：24h TTL + 每群 300 条），永不进个人库
// 接口里的 user_id 参数在群实例语义上是 group_id（"owner"）
class ImageAssetStore
{
public:
    ImageAssetStore(const std::string &dbPath, const std::string &assetDirectory,
                    std::string tableName = "image_assets", std::string idPrefix = "img_");
    ~ImageAssetStore();

    ImageAssetStore(const ImageAssetStore &) = delete;
    ImageAssetStore &operator=(const ImageAssetStore &) = delete;

    std::optional<ImageAsset> importFromUrl(uint64_t user_id, const std::string &url,
                                            int64_t source_message_id);
    // 指定来源与提示词的下载入库（生图接口只回 url 时按 generated 入库）
    std::optional<ImageAsset> importFromUrl(uint64_t user_id, const std::string &url,
                                            const std::string &source, const std::string &prompt,
                                            int64_t source_message_id);
    std::optional<ImageAsset> saveBase64(uint64_t user_id, const std::string &base64,
                                         const std::string &source, const std::string &prompt,
                                         int64_t source_message_id);
    std::optional<ImageAsset> find(uint64_t user_id, const std::string &asset_id) const;
    std::optional<ImageAsset> findLatest(uint64_t user_id, const std::string &source) const;
    // 不分来源的最近一张（工具解析的最后兜底：群友说"看看这个图"时多半指最新那张）
    std::optional<ImageAsset> findLatest(uint64_t user_id) const;
    // 按来源消息 ID 找（群实例：observe 先入库，Message 的 @ 路径据此复用，不重复下载）
    std::optional<ImageAsset> findBySourceMessage(uint64_t user_id, int64_t source_message_id) const;
    // 按本地文件路径找（群实例：出站 ImageMessage 只带路径，入库占位符要 asset_id）
    std::optional<ImageAsset> findByLocalPath(uint64_t user_id, const std::string &local_path) const;
    std::vector<ImageAsset> list(uint64_t user_id, const std::string &source, int limit) const;
    std::string readBase64(const ImageAsset &asset) const;
    void attachToConversation(uint64_t user_id, const std::string &asset_id, int64_t message_id);

    void clearUser(uint64_t user_id);
    void removeByConversationFrom(uint64_t user_id, int64_t first_message_id);

    // 短时缓冲清理（群实例用）：删除 created_at < now-ttlSeconds 的行，以及每个 owner
    // 超出 perOwnerCap 的最旧行；文件一并删除。返回删除条数
    std::size_t prune(int64_t now, int64_t ttlSeconds, int perOwnerCap);

    const std::string &tableName() const { return table; }

private:
    std::optional<ImageAsset> saveBytes(uint64_t user_id, const std::string &bytes,
                                        const std::string &mime_type, const std::string &source,
                                        const std::string &prompt, int64_t source_message_id);
    std::optional<ImageAsset> insertAsset(const ImageAsset &asset);
    std::vector<ImageAsset> query(const std::string &sql, uint64_t user_id,
                                  const std::string &asset_id, const std::string &source,
                                  int limit) const;
    void removeFiles(const std::vector<ImageAsset> &assets) const;
    std::string selectPrefix() const;

    sqlite3 *db = nullptr;
    std::string assetDirectory;
    const std::string table;
    const std::string idPrefix;
    mutable std::mutex mutex;
};

#endif // IMAGE_ASSET_STORE_H
