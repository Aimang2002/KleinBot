#ifndef LOG_H
#define LOG_H

#include <iostream>

#define LOG_DEBUG(message)        \
    std::cout << "\033[38;5;208m" \
              << "DEBUG: " << message << "\033[0m" << std::endl

#define LOG_INFO(message)   \
    std::cout << "\033[32m" \
              << "INFO: " << message << "\033[0m" << std::endl

#define LOG_WARNING(message) \
    std::cout << "\033[33m"  \
              << "WARNING: " << message << "\033[0m" << std::endl

#define LOG_ERROR(message)  \
    std::cerr << "\033[31m" \
              << "ERROR: " << message << "\033[0m" << std::endl

#define LOG_FATAL(message)    \
    std::cerr << "\033[1;31m" \
              << "FATAL: " << message << "\033[0m" << std::endl

#endif