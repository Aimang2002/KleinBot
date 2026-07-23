include(FetchContent)

set(KLEINBOT_CURL_VERSION "8.21.0")
set(KLEINBOT_SQLITE_VERSION "3530300")

function(kleinbot_configure_dependencies)
    add_library(kleinbot_boost INTERFACE)
    add_library(kleinbot_curl INTERFACE)
    add_library(kleinbot_sqlite INTERFACE)

    if(KLEINBOT_USE_BUNDLED_DEPS)
        set(KLEINBOT_BOOST_ARCHIVE
            "${CMAKE_CURRENT_SOURCE_DIR}/Library/boost/kleinbot-boost-beast-asio-1.91.0.tar.xz")
        set(KLEINBOT_BOOST_SOURCE_DIR
            "${CMAKE_CURRENT_BINARY_DIR}/_deps/kleinbot-boost-1.91.0")
        file(SHA256 "${KLEINBOT_BOOST_ARCHIVE}" KLEINBOT_BOOST_ARCHIVE_HASH)
        if(NOT KLEINBOT_BOOST_ARCHIVE_HASH STREQUAL
               "d19684cd76eb6b0b009cee11f2d204cab6d41a5d32ed97e4d244afeed51c6ebf")
            message(FATAL_ERROR "Bundled Boost archive checksum mismatch")
        endif()
        if(NOT EXISTS "${KLEINBOT_BOOST_SOURCE_DIR}/boost/beast.hpp")
            file(MAKE_DIRECTORY "${KLEINBOT_BOOST_SOURCE_DIR}")
            file(ARCHIVE_EXTRACT
                INPUT "${KLEINBOT_BOOST_ARCHIVE}"
                DESTINATION "${KLEINBOT_BOOST_SOURCE_DIR}"
            )
        endif()
        target_include_directories(kleinbot_boost INTERFACE
            "${KLEINBOT_BOOST_SOURCE_DIR}"
        )
        target_compile_definitions(kleinbot_boost INTERFACE
            BOOST_ERROR_CODE_HEADER_ONLY
        )

        FetchContent_Declare(
            kleinbot_sqlite_source
            URL "https://www.sqlite.org/2026/sqlite-amalgamation-${KLEINBOT_SQLITE_VERSION}.zip"
            URL_HASH SHA3_256=d45c688a8cb23f68611a894a756a12d7eb6ab6e9e2468ca70adbeab3808b5ab9
        )
        FetchContent_GetProperties(kleinbot_sqlite_source)
        if(NOT kleinbot_sqlite_source_POPULATED)
            FetchContent_Populate(kleinbot_sqlite_source)
        endif()
        add_library(kleinbot_sqlite_bundled STATIC
            "${kleinbot_sqlite_source_SOURCE_DIR}/sqlite3.c"
        )
        target_include_directories(kleinbot_sqlite_bundled PUBLIC
            "${kleinbot_sqlite_source_SOURCE_DIR}"
        )
        target_link_libraries(kleinbot_sqlite INTERFACE kleinbot_sqlite_bundled)

        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
        set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
        set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
        set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
        set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
        set(HTTP_ONLY ON CACHE BOOL "" FORCE)
        set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
        set(CURL_ZLIB OFF CACHE STRING "" FORCE)
        set(CURL_BROTLI OFF CACHE STRING "" FORCE)
        set(CURL_ZSTD OFF CACHE STRING "" FORCE)
        set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
        set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
        set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
        if(WIN32)
            set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
            set(CURL_TARGET_WINDOWS_VERSION "0x0A00" CACHE STRING "" FORCE)
        endif()

        FetchContent_Declare(
            kleinbot_curl_source
            URL "https://curl.se/download/curl-${KLEINBOT_CURL_VERSION}.tar.xz"
            URL_HASH SHA256=aa1b66a70eace83dc624508745646c08ae561de512ab403adffb93ac87fc72e6
        )
        FetchContent_MakeAvailable(kleinbot_curl_source)
        target_link_libraries(kleinbot_curl INTERFACE CURL::libcurl)
    else()
        find_package(Boost REQUIRED)
        find_package(CURL REQUIRED)
        find_package(SQLite3 REQUIRED)

        if(TARGET Boost::headers)
            target_link_libraries(kleinbot_boost INTERFACE Boost::headers)
        else()
            target_link_libraries(kleinbot_boost INTERFACE Boost::boost)
        endif()
        target_link_libraries(kleinbot_curl INTERFACE CURL::libcurl)
        target_link_libraries(kleinbot_sqlite INTERFACE SQLite::SQLite3)
    endif()
endfunction()

function(kleinbot_configure_test_dependencies)
    if(TARGET GTest::gtest_main)
        return()
    endif()

    find_package(GTest QUIET)
    if(TARGET GTest::gtest_main)
        return()
    endif()

    if(KLEINBOT_USE_BUNDLED_DEPS)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.17.0
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(googletest)
    endif()
endfunction()
