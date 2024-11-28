cmake_minimum_required(VERSION 3.11)
project("Qbot2.3.2")

# 添加宏定义
option(DEBUG "Build the debug version" OFF)
if(DEBUG)
  add_compile_definitions(DEBUG)
  set(EXECUTABLE_NAME KleinQBot2.3.2_DEBUG)
else()
  set(EXECUTABLE_NAME KleinQBot2.3.2)
endif()

file(GLOB_RECURSE SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)# 项目源文件所在位置

include_directories(${CMAKE_CURRENT_SOURCE_DIR}) # 项目头文件所在位置
# 添加头文件搜索路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/Library/rapidjson
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ComputerStatus
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ConfigManager
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Database
    ${CMAKE_CURRENT_SOURCE_DIR}/src/JsonParse
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Log
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Message
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Network
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TimingTast
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MessageQueue
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ModelApiCaller
    # 添加其他子目录头文件路径
)

# 查找 libcurl 库
find_package(CURL REQUIRED)

# 查找 Boost 库
find_package(Boost REQUIRED COMPONENTS system) # 指定需要的 Boost 组件

# 添加可执行文件，并链接 libcurl 和 Boost 库
add_executable(${EXECUTABLE_NAME} ${SRC})
target_link_libraries(${EXECUTABLE_NAME} PRIVATE CURL::libcurl Boost::system)