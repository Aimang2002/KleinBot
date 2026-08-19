#ifndef INBOUND_MESSAGE_H
#define INBOUND_MESSAGE_H

#include <cstddef>
#include <cstdint>
#include <string>

struct InboundMessage
{
    std::uint64_t user_id = 0;
    std::string nickname;
    std::string card;

    std::uint64_t group_id = 0;
    std::string message_type;
    std::string post_type;

    std::string raw_message;
    std::string plain_text;
    std::string message_data_url;

    std::int64_t message_id = 0;
    std::int64_t message_timestamp = 0;
    std::size_t payload_size_bytes = 0;
};

#endif // INBOUND_MESSAGE_H
