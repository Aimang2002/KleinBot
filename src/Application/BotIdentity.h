#ifndef BOT_IDENTITY_H
#define BOT_IDENTITY_H

#include <cstdint>
#include <string>

struct BotIdentity
{
    std::uint64_t id = 0;
    std::uint64_t managerId = 0;
    std::string name = "Klein";
};

#endif
