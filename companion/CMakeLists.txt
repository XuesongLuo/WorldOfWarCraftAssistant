find_package(nlohmann_json CONFIG REQUIRED)

add_library(wowai_core STATIC
    src/app/build_info.cpp
    src/codex/mock_host.cpp
    src/codex/protocol.cpp
)

target_include_directories(wowai_core PUBLIC include)
target_compile_features(wowai_core PUBLIC cxx_std_20)
target_link_libraries(wowai_core PUBLIC nlohmann_json::nlohmann_json)
wowai_enable_warnings(wowai_core)

add_executable(wowai_companion src/app/main.cpp)
target_link_libraries(wowai_companion PRIVATE wowai_core)
wowai_enable_warnings(wowai_companion)

add_executable(wowai_protocol_mock src/codex/mock_host_main.cpp)
target_link_libraries(wowai_protocol_mock PRIVATE wowai_core)
wowai_enable_warnings(wowai_protocol_mock)

if(WOWAI_BUILD_TESTS)
    find_package(Catch2 3 CONFIG REQUIRED)

    add_executable(wowai_tests
        tests/codex_protocol/contract_tests.cpp
        tests/unit/build_info_tests.cpp
    )
    target_link_libraries(wowai_tests PRIVATE wowai_core Catch2::Catch2WithMain)
    target_compile_definitions(wowai_tests PRIVATE
        WOWAI_CONTRACT_FIXTURES="${PROJECT_SOURCE_DIR}/contracts/tests/validation-cases.json"
    )
    wowai_enable_warnings(wowai_tests)

    include(Catch)
    catch_discover_tests(wowai_tests)

    add_custom_target(verify
        COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" -C $<CONFIG> --output-on-failure
        COMMAND pwsh -NoProfile -File "${PROJECT_SOURCE_DIR}/scripts/verify-repository.ps1"
        DEPENDS wowai_companion wowai_protocol_mock wowai_tests
        USES_TERMINAL
        COMMENT "Running C++ tests and repository checks"
    )
endif()
