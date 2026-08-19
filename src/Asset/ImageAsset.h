#ifndef IMAGE_ASSET_H
#define IMAGE_ASSET_H

#include <cstdint>
#include <string>

struct ImageAsset
{
    std::string asset_id;
    uint64_t user_id = 0;
    std::string source; // inbound / generated
    std::string local_path;
    std::string mime_type;
    std::string prompt;
    int64_t source_message_id = 0;
    int64_t created_at = 0;
};

#endif // IMAGE_ASSET_H
