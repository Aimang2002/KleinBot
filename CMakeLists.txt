cmake_minimum_required(VERSION 3.11)
project("Qbot2.4.0")

option(DEBUG "Build the debug version" ON)
if(DEBUG)
  add_compile_definitions(DEBUG)
  set(EXECUTABLE_NAME KleinQBot2.4.0_DEBUG)
  set(CMAKE_BUILD_TYPE Debug)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0")
else()
  set(EXECUTABLE_NAME KleinQBot2.4.0)
  set(CMAKE_BUILD_TYPE Release)
endif()

# 只在 Windows 上设置这些 Boost 选项
if(WIN32)
    if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        set(Boost_USE_DEBUG_LIBS ON)
        set(Boost_USE_RELEASE_LIBS OFF)
    elseif(${CMAKE_BUILD_TYPE} STREQUAL "Release")
        set(Boost_USE_DEBUG_LIBS OFF)
        set(Boost_USE_RELEASE_LIBS ON)
    endif()
endif()

file(GLOB_RECURSE SRC 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/submodules/CloudMusicID/*.cpp
)

# 添加头文件搜索路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Library/rapidjson
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Command
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/submodules/CloudMusicID
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UserSession
)

# 只在 Windows 上设置 CURL 和 Boost 的特定路径
if(WIN32)
    set(CURL_INCLUDE_DIR "D:/develop/curl/include")
    set(CURL_LIBRARY "D:/develop/curl/lib/libcurl.dll.a")
    set(BOOST_ROOT "D:/develop/boost_1_86_0")
    set(BOOST_LIBRARYDIR "D:/develop/boost_1_86_0/stage/lib") 

    set(Boost_USE_STATIC_LIBS ON)
    set(Boost_USE_MULTITHREADED ON)
    set(Boost_COMPILER "-mgw11")
    set(Boost_ARCHITECTURE "-x64")
    set(Boost_DEBUG ON)
endif()

find_package(Boost REQUIRED COMPONENTS system) 

add_executable(${EXECUTABLE_NAME} ${SRC})
if(WIN32)
    target_include_directories(${EXECUTABLE_NAME} PRIVATE ${CURL_INCLUDE_DIR})
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE ${CURL_LIBRARY} ${Boost_LIBRARIES} ws2_32 iphlpapi)
else()
    find_package(CURL REQUIRED)
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE CURL::libcurl Boost::system)
endif()
