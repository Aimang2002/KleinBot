add_executable(kleinbot_tests
    ActionTests.cpp
    CommandAdapterTests.cpp
    StorageIntegrationTests.cpp
)

target_link_libraries(kleinbot_tests
    PRIVATE
        kleinbot_core
        GTest::gtest_main
)

target_compile_features(kleinbot_tests PRIVATE cxx_std_17)

include(GoogleTest)
gtest_discover_tests(kleinbot_tests)
