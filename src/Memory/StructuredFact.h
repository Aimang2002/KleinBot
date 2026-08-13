#ifndef STRUCTURED_FACT_H
#define STRUCTURED_FACT_H

#include <cstdint>
#include <string>

struct StructuredFactQuery
{
    std::string subject;
    std::string predicate;
    std::string temporal = "current";
};

struct StructuredFactResult
{
    uint64_t user_id = 0;
    std::string subject_key;
    std::string subject_type;
    std::string subject_name;
    std::string subject_aliases;
    std::string predicate;
    std::string value_text;
    std::string memory_type;
    std::string canonical_text;
    std::string search_text;
    double importance = 0.5;
    double confidence = 0.5;
    int64_t source_start_id = 0;
    int64_t source_end_id = 0;
    int64_t valid_from_source_id = 0;
    int64_t valid_to_source_id = 0;
    std::string status;
    std::string temporal;
    double relevance = 0.0;
};

#endif // STRUCTURED_FACT_H
