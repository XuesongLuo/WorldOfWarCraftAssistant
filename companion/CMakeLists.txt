include(FetchContent)

add_library(wowai_core STATIC
    src/app/build_info.cpp
)

target_include_directories(wowai_core PUBLIC include)
target_compile_features(wowai_core PUBLIC cxx_std_20)
wowai_enable_warnings(wowai_core)

add_executable(wowai_companion src/app/main.cpp)
target_link_libraries(wowai_companion PRIVATE wowai_core)
wowai_enable_warnings(wowai_companion)

if(WOWAI_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        URL https://codeload.github.com/catchorg/Catch2/tar.gz/refs/tags/v3.8.1
        URL_HASH SHA256=18B3F70AC80FCCC340D8C6FF0F339B2AE64944782F8D2FCA2BD705CF47CADB79
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    if(MSVC)
        target_compile_options(Catch2 PRIVATE /FS)
        target_compile_options(Catch2WithMain PRIVATE /FS)
    endif()

    add_executable(wowai_tests tests/unit/build_info_tests.cpp)
    target_link_libraries(wowai_tests PRIVATE wowai_core Catch2::Catch2WithMain)
    wowai_enable_warnings(wowai_tests)

    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    include(Catch)
    catch_discover_tests(wowai_tests)

    add_custom_target(verify
        COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" -C $<CONFIG> --output-on-failure
        COMMAND pwsh -NoProfile -File "${PROJECT_SOURCE_DIR}/scripts/verify-repository.ps1"
        DEPENDS wowai_companion wowai_tests
        USES_TERMINAL
        COMMENT "Running C++ tests and repository checks"
    )
endif()
