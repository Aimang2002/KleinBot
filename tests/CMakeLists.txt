add_executable(kleinbot_tests
    ActionTests.cpp
    CommandAdapterTests.cpp
    ConfigReloadTests.cpp
    CurlRequestControlTests.cpp
    NetworkAuthTests.cpp
    OutboundInfrastructureTests.cpp
    StorageIntegrationTests.cpp
    TaskSchedulerTests.cpp
    TransportConfigTests.cpp
)

add_executable(transport_contract_driver TransportContractDriver.cpp)

target_link_libraries(kleinbot_tests
    PRIVATE
        kleinbot_core
        GTest::gtest_main
)

target_link_libraries(transport_contract_driver PRIVATE kleinbot_core)

target_compile_features(kleinbot_tests PRIVATE cxx_std_17)
target_compile_features(transport_contract_driver PRIVATE cxx_std_17)

include(GoogleTest)
gtest_discover_tests(kleinbot_tests)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME TransportContractPython
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/transport_contract_test.py
            --driver $<TARGET_FILE:transport_contract_driver>
    )
    add_test(
        NAME ArchitectureDependencyPython
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/architecture_dependency_test.py
            --source-root ${PROJECT_SOURCE_DIR}/src
    )
else()
    message(WARNING "Python3 not found; Python contract tests will not be registered")
endif()
