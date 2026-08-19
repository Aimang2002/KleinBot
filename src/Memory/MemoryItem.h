#ifndef MEMORY_ITEM_H
#define MEMORY_ITEM_H

#include <cstdint>
#include <string>
#include <vector>

struct MemoryItem
{
    int64_t id = 0;
    uint64_t user_id = 0;
    std::string memory_key;
    std::string memory_type;
    std::string canonical_text;
    std::string search_text;
    std::string subject_key;
    std::string subject_type;
    std::string subject_name;
    std::vector<std::string> subject_aliases;
    std::string predicate;
    std::string value_text;
    double importance = 0.5;
    double confidence = 0.5;
    int64_t source_start_id = 0;
    int64_t source_end_id = 0;
    int64_t created_at = 0;
    int64_t updated_at = 0;
    bool active = true;
    double relevance = 0.0;
};

struct MemoryTurn
{
    std::string user_text;
    std::string assistant_text;
    int64_t source_start_id = 0;
    int64_t source_end_id = 0;
};

struct MemoryMutation
{
    std::string action;
    MemoryItem item;
};

#endif // MEMORY_ITEM_H
