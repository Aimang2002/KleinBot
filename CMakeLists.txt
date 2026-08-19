cmake_minimum_required(VERSION 3.21)
project("Qbot2.4.0" LANGUAGES C CXX)

option(DEBUG "Build the debug version" ON)
if(WIN32)
    set(KLEINBOT_BUNDLED_DEPS_DEFAULT ON)
else()
    set(KLEINBOT_BUNDLED_DEPS_DEFAULT OFF)
endif()
option(KLEINBOT_USE_BUNDLED_DEPS
       "Download and build pinned third-party dependencies"
       ${KLEINBOT_BUNDLED_DEPS_DEFAULT})
unset(KLEINBOT_BUNDLED_DEPS_DEFAULT)
option(KLEINBOT_STATIC_MINGW_RUNTIME
       "Statically link the MinGW GCC, C++ and pthread runtimes"
       ON)

if(MINGW)
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles(
        "#include <thread>
         int main()
         {
             std::thread worker([] {});
             worker.join();
             return 0;
         }"
        KLEINBOT_MINGW_STD_THREAD_AVAILABLE
    )
    if(NOT KLEINBOT_MINGW_STD_THREAD_AVAILABLE)
        message(FATAL_ERROR
            "KleinBot requires a MinGW-w64 toolchain with POSIX thread support")
    endif()
endif()

if(DEBUG)
    add_compile_definitions(DEBUG)
    set(EXECUTABLE_NAME KleinQBot2.4.0_DEBUG)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
else()
    set(EXECUTABLE_NAME KleinQBot2.4.0)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

file(GLOB_RECURSE CORE_SRC
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)
list(REMOVE_ITEM CORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Library/rapidjson
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Command
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ComputerStatus
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ConfigManager
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Persistence
    ${CMAKE_CURRENT_SOURCE_DIR}/src/JsonParse
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Log
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Message
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Network
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TimingTast
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MessageQueue
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ModelApiCaller
    ${CMAKE_CURRENT_SOURCE_DIR}/src/submodules/CloudMusicID
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UserSession
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ModelRegistry
)

include(cmake/Dependencies.cmake)
kleinbot_configure_dependencies()

add_library(kleinbot_core STATIC ${CORE_SRC})
target_compile_features(kleinbot_core PUBLIC cxx_std_17)
target_link_libraries(kleinbot_core
    PUBLIC
        kleinbot_boost
        kleinbot_curl
        kleinbot_sqlite
)
if(WIN32)
    target_compile_definitions(kleinbot_core PUBLIC
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
    )
    target_link_libraries(kleinbot_core PUBLIC ws2_32 iphlpapi mswsock)
endif()

add_executable(${EXECUTABLE_NAME} src/main.cpp)
target_link_libraries(${EXECUTABLE_NAME} PRIVATE kleinbot_core)
if(MINGW AND KLEINBOT_STATIC_MINGW_RUNTIME)
    target_link_options(${EXECUTABLE_NAME} PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
    )
endif()

include(CTest)
if(BUILD_TESTING)
    kleinbot_configure_test_dependencies()
    if(TARGET GTest::gtest_main)
        add_subdirectory(tests)
    else()
        message(WARNING "GoogleTest not found; tests will not be built")
    endif()
endif()
