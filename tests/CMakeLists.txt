add_executable(kleinbot_tests
    ActionTests.cpp
    CommandAdapterTests.cpp
    ConfigPanelServerTests.cpp
    ConfigReloadTests.cpp
    ConfigTemplateTests.cpp
    ConfigWriterTests.cpp
    CurlRequestControlTests.cpp
    MemoryRecallTests.cpp
    ModelCatalogTests.cpp
    ModelReadinessTests.cpp
    ModelRegistryTests.cpp
    MultimodalRoutingTests.cpp
    NetworkAuthTests.cpp
    OutboundInfrastructureTests.cpp
    ReminderTests.cpp
    StorageIntegrationTests.cpp
    TaskSchedulerTests.cpp
    TransportConfigTests.cpp
    WebFetchTests.cpp
    WebSearchTests.cpp
)

add_executable(transport_contract_driver TransportContractDriver.cpp)

target_link_libraries(kleinbot_tests
    PRIVATE
        kleinbot_core
        GTest::gtest_main
)

# 面板页面测试依赖：GET / 从测试进程工作目录读取 panel.html；
# 独立目标 + 文件依赖，纯页面改动在增量构建时也会同步
add_custom_target(panel_page_sync_tests ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/src/WebUI/panel.html"
            "$<TARGET_FILE_DIR:kleinbot_tests>/panel.html"
    DEPENDS "${PROJECT_SOURCE_DIR}/src/WebUI/panel.html"
)
add_dependencies(kleinbot_tests panel_page_sync_tests)

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
