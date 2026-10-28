#include "UniqueId.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>
#include <sstream>

namespace utils
{
std::string uniqueId(const char *prefix)
{
    static std::atomic<std::uint64_t> counter{0};
    static std::mutex randomMutex;
    static std::mt19937_64 engine(std::random_device{}());

    std::uint64_t randomValue = 0;
    {
        std::lock_guard<std::mutex> lock(randomMutex);
        randomValue = engine();
    }
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::ostringstream output;
    output << prefix << '_' << std::hex
           << static_cast<std::uint64_t>(now) << '_'
           << counter.fetch_add(1) << '_' << randomValue;
    return output.str();
}
} // namespace utils
