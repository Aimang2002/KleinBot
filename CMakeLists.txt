cmake_minimum_required(VERSION 3.21)
project(KleinBot VERSION 2.4.0 LANGUAGES C CXX)

# 版本号唯一来源：上方的 project(... VERSION ...)；渠道后缀单独控制，
# 发布正式版时 -DKLEINBOT_VERSION_SUFFIX="" 或改默认值即可，无需动源码
set(KLEINBOT_VERSION_SUFFIX "_betaVersion" CACHE STRING "版本渠道后缀，正式版置空")

# 版本头 generated/KleinVersion.h 改由 cmake/RefreshVersion.cmake 生成（configure 时一次、
# 每次构建时刷新），短哈希同时写入 kleinbot_git_hash.txt 供产物重命名使用
find_program(GIT_EXECUTABLE git)

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
    set(KLEINBOT_OUTPUT_NAME KleinBot_debug)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
else()
    set(KLEINBOT_OUTPUT_NAME KleinBot)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# 目标架构归一化，供版本头与产物命名使用（MinGW 下 CMAKE_SYSTEM_PROCESSOR 常为 AMD64）。
# 手动交叉配置（无 toolchain file）时 CMAKE_SYSTEM_PROCESSOR 可能为空，回退到编译器
# -dumpmachine 三元组前缀（如 x86_64-w64-mingw32 → x86_64）
set(KLEINBOT_ARCH_RAW "")
if(CMAKE_SYSTEM_PROCESSOR)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" KLEINBOT_ARCH_RAW)
else()
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} -dumpmachine
        OUTPUT_VARIABLE kleinbot_dumpmachine
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE kleinbot_dumpmachine_result
    )
    if(kleinbot_dumpmachine_result EQUAL 0 AND kleinbot_dumpmachine MATCHES "^([^-]+)")
        string(TOLOWER "${CMAKE_MATCH_1}" KLEINBOT_ARCH_RAW)
    endif()
endif()
if(KLEINBOT_ARCH_RAW MATCHES "^(amd64|x86_64|x64)$")
    set(KLEINBOT_ARCH x86_64)
elseif(KLEINBOT_ARCH_RAW MATCHES "^(i[3-6]86|x86)$")
    set(KLEINBOT_ARCH i686)
elseif(KLEINBOT_ARCH_RAW MATCHES "^(arm64|aarch64)$")
    set(KLEINBOT_ARCH arm64)
elseif(KLEINBOT_ARCH_RAW MATCHES "^arm")
    set(KLEINBOT_ARCH arm)
elseif(KLEINBOT_ARCH_RAW STREQUAL "")
    set(KLEINBOT_ARCH unknown)
else()
    set(KLEINBOT_ARCH "${KLEINBOT_ARCH_RAW}")
endif()
set(KLEINBOT_COMPILER "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

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
target_include_directories(kleinbot_core PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated")
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

# 版本头刷新：configure 时先执行一次保证头文件存在；之后每次构建都重新查询 git
# 短哈希并按需更新头文件（内容不变不落盘，不触发重编），commit 后无需手动重新 configure
set(KLEINBOT_REFRESH_ARGS
    "-DKLEINBOT_TEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/KleinVersion.h.in"
    "-DKLEINBOT_HEADER_OUT=${CMAKE_CURRENT_BINARY_DIR}/generated/KleinVersion.h"
    "-DKLEINBOT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DKLEINBOT_VERSION=${PROJECT_VERSION}"
    "-DKLEINBOT_VERSION_SUFFIX=${KLEINBOT_VERSION_SUFFIX}"
    "-DKLEINBOT_ARCH=${KLEINBOT_ARCH}"
    "-DKLEINBOT_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DKLEINBOT_COMPILER=${KLEINBOT_COMPILER}"
    "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
)
execute_process(COMMAND ${CMAKE_COMMAND} ${KLEINBOT_REFRESH_ARGS}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RefreshVersion.cmake")
add_custom_target(kleinbot_refresh_version
    COMMAND ${CMAKE_COMMAND} ${KLEINBOT_REFRESH_ARGS}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RefreshVersion.cmake"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/KleinVersion.h"
    COMMENT "刷新版本信息（git 短哈希）"
)
# 挂到核心库上：可执行文件与 tests 都经由 kleinbot_core 拿到最新版本头
add_dependencies(kleinbot_core kleinbot_refresh_version)

add_executable(KleinBot src/main.cpp)
set_target_properties(KleinBot PROPERTIES OUTPUT_NAME ${KLEINBOT_OUTPUT_NAME})
target_link_libraries(KleinBot PRIVATE kleinbot_core)
if(MINGW AND KLEINBOT_STATIC_MINGW_RUNTIME)
    target_link_options(KleinBot PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
    )
endif()

# Web 面板页面是运行时资源：文件变化即同步到可执行文件目录（POST_BUILD 只在重链时触发，
# 纯前端改动不会重链，所以用依赖 panel.html 的独立目标保证增量构建也能同步）
add_custom_target(panel_page_sync ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/WebUI/panel.html"
            "$<TARGET_FILE_DIR:KleinBot>/panel.html"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/WebUI/panel.html"
)
add_dependencies(KleinBot panel_page_sync)

include(CTest)
if(BUILD_TESTING)
    kleinbot_configure_test_dependencies()
    if(TARGET GTest::gtest_main)
        add_subdirectory(tests)
    else()
        message(WARNING "GoogleTest not found; tests will not be built")
    endif()
endif()
