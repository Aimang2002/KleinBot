cmake_minimum_required(VERSION 3.11)
project("Qbot2.3.3")

option(DEBUG "Build the debug version" OFF)
if(DEBUG)
  add_compile_definitions(DEBUG)
  set(EXECUTABLE_NAME KleinQBot2.3.3_DEBUG)
  set(CMAKE_BUILD_TYPE Debug)
else()
  set(EXECUTABLE_NAME KleinQBot2.3.3)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(Boost_USE_DEBUG_LIBS ON)
    set(Boost_USE_RELEASE_LIBS OFF)
elseif(${CMAKE_BUILD_TYPE} STREQUAL "Release")
    set(Boost_USE_DEBUG_LIBS OFF)
    set(Boost_USE_RELEASE_LIBS ON)
endif()

file(GLOB_RECURSE SRC 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/submodules/CloudMusicID/*.cpp
)

# 添加头文件搜索路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/submodules/CloudMusicID
    # 添加其他子目录头文件路径
)

# 在Windows上设置CURL和Boost的路径
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
