find_package(nlohmann_json CONFIG REQUIRED)
find_package(wil CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

set(WOWAI_WEBVIEW2_ROOT "${PROJECT_SOURCE_DIR}/.tools/webview2-sdk" CACHE PATH
    "Path to the locked Microsoft WebView2 SDK")
set(WOWAI_WEBVIEW2_INCLUDE "${WOWAI_WEBVIEW2_ROOT}/build/native/include")
set(WOWAI_WEBVIEW2_LOADER "${WOWAI_WEBVIEW2_ROOT}/build/native/x64/WebView2LoaderStatic.lib")
if(NOT EXISTS "${WOWAI_WEBVIEW2_INCLUDE}/WebView2.h" OR NOT EXISTS "${WOWAI_WEBVIEW2_LOADER}")
    message(FATAL_ERROR "Locked WebView2 SDK is missing. Run scripts/Bootstrap-Dependencies.ps1")
endif()
add_library(wowai_webview2_loader STATIC IMPORTED GLOBAL)
set_target_properties(wowai_webview2_loader PROPERTIES
    IMPORTED_LOCATION "${WOWAI_WEBVIEW2_LOADER}"
    INTERFACE_INCLUDE_DIRECTORIES "${WOWAI_WEBVIEW2_INCLUDE}"
)
add_library(WebView2::LoaderStatic ALIAS wowai_webview2_loader)

add_library(wowai_core STATIC
    src/app/application_shell.cpp
    src/app/build_info.cpp
    src/app/lifecycle.cpp
    src/app/settings_window.cpp
    src/capture/anchor_detector.cpp
    src/capture/calibration.cpp
    src/capture/image_processing.cpp
    src/capture/image_encoding.cpp
    src/capture/scene_awareness.cpp
    src/capture/selection_store.cpp
    src/capture/visual_bridge.cpp
    src/capture/window_capture.cpp
    src/capture/window_discovery.cpp
    src/codex/assistant_session.cpp
    src/codex/mock_host.cpp
    src/codex/host_client.cpp
    src/codex/host_process.cpp
    src/codex/protocol.cpp
    src/overlay/overlay_policy.cpp
    src/overlay/overlay_window.cpp
    src/overlay/web_message_bridge.cpp
    src/platform/single_instance.cpp
    src/platform/global_hotkey.cpp
    src/storage/application_paths.cpp
    src/storage/credential_store.cpp
    src/storage/local_data.cpp
    src/storage/local_database.cpp
    src/storage/safe_log.cpp
    src/storage/settings.cpp
)

target_include_directories(wowai_core PUBLIC include)
target_compile_features(wowai_core PUBLIC cxx_std_20)
target_link_libraries(wowai_core
    PUBLIC
        nlohmann_json::nlohmann_json
        WIL::WIL
        spdlog::spdlog
        unofficial::sqlite3::sqlite3
    PRIVATE
        gdi32
        d3d11
        bcrypt
        crypt32
        dcomp
        dwmapi
        dxgi
        ole32
        windowscodecs
        shell32
        WebView2::LoaderStatic
        user32
        windowsapp
)
wowai_enable_warnings(wowai_core)

add_executable(wowai_companion WIN32 src/app/main.cpp)
target_link_libraries(wowai_companion PRIVATE wowai_core)
wowai_enable_warnings(wowai_companion)

add_executable(wowai_protocol_mock src/codex/mock_host_main.cpp)
target_link_libraries(wowai_protocol_mock PRIVATE wowai_core)
wowai_enable_warnings(wowai_protocol_mock)

if(WOWAI_BUILD_TESTS)
    find_package(Catch2 3 CONFIG REQUIRED)

    add_custom_command(
        OUTPUT "${PROJECT_SOURCE_DIR}/codex-host/dist/index.js"
        COMMAND pwsh -NoProfile -File "${PROJECT_SOURCE_DIR}/scripts/Invoke-Npm.ps1" run build
        DEPENDS
            "${PROJECT_SOURCE_DIR}/codex-host/src/index.ts"
            "${PROJECT_SOURCE_DIR}/codex-host/src/app-server/client.ts"
            "${PROJECT_SOURCE_DIR}/codex-host/src/app-server/process.ts"
            "${PROJECT_SOURCE_DIR}/codex-host/src/app-server/protocol.ts"
            "${PROJECT_SOURCE_DIR}/codex-host/src/app-server/tool-policy.ts"
            "${PROJECT_SOURCE_DIR}/codex-host/src/cloud-model/provider.ts"
            "${PROJECT_SOURCE_DIR}/codex-host/src/host/application-directories.ts"
            "${PROJECT_SOURCE_DIR}/codex-host/src/host/json-line-reader.ts"
            "${PROJECT_SOURCE_DIR}/codex-host/src/host/server.ts"
            "${PROJECT_SOURCE_DIR}/codex-host/src/local-model/provider.ts"
            "${PROJECT_SOURCE_DIR}/codex-host/src/runtime/app-server-runtime.ts"
            "${PROJECT_SOURCE_DIR}/codex-host/src/runtime/errors.ts"
            "${PROJECT_SOURCE_DIR}/codex-host/src/runtime/mock-runtime.ts"
            "${PROJECT_SOURCE_DIR}/codex-host/src/runtime/runtime-lock.ts"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Bundling the locked TypeScript Codex Host"
        VERBATIM
    )
    add_custom_target(wowai_codex_host_bundle ALL
        DEPENDS "${PROJECT_SOURCE_DIR}/codex-host/dist/index.js")

    add_executable(wowai_host_process_fixture tests/fixtures/host_process_child.cpp)
    wowai_enable_warnings(wowai_host_process_fixture)

    add_executable(wowai_tests
        tests/codex_protocol/contract_tests.cpp
        tests/unit/build_info_tests.cpp
        tests/unit/anchor_detector_tests.cpp
        tests/unit/assistant_session_tests.cpp
        tests/unit/calibration_tests.cpp
        tests/unit/image_processing_tests.cpp
        tests/unit/image_encoding_tests.cpp
        tests/unit/scene_awareness_tests.cpp
        tests/unit/host_process_tests.cpp
        tests/unit/lifecycle_tests.cpp
        tests/unit/global_hotkey_tests.cpp
        tests/unit/local_storage_tests.cpp
        tests/unit/overlay_policy_tests.cpp
        tests/unit/resource_tests.cpp
        tests/unit/selection_store_tests.cpp
        tests/unit/single_instance_tests.cpp
        tests/unit/visual_bridge_tests.cpp
        tests/unit/window_discovery_tests.cpp
        tests/unit/web_message_bridge_tests.cpp
    )
    target_link_libraries(wowai_tests PRIVATE wowai_core Catch2::Catch2WithMain)
    target_compile_definitions(wowai_tests PRIVATE
        WOWAI_CONTRACT_FIXTURES="${PROJECT_SOURCE_DIR}/contracts/tests/validation-cases.json"
        WOWAI_ANCHOR_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/anchor-samples.json"
        WOWAI_VISION_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/vision-samples.json"
        WOWAI_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
        WOWAI_HOST_PROCESS_FIXTURE="$<TARGET_FILE:wowai_host_process_fixture>"
        WOWAI_NODE_EXECUTABLE="${PROJECT_SOURCE_DIR}/.tools/node/node.exe"
        WOWAI_CODEX_HOST_BUNDLE="${PROJECT_SOURCE_DIR}/codex-host/dist/index.js"
    )
    add_dependencies(wowai_tests wowai_codex_host_bundle wowai_host_process_fixture)
    wowai_enable_warnings(wowai_tests)

    add_executable(wowai_overlay_component_test
        tests/component/overlay_component_test.cpp
    )
    target_link_libraries(wowai_overlay_component_test PRIVATE wowai_core)
    wowai_enable_warnings(wowai_overlay_component_test)
    add_test(NAME "WebView2 composition overlay initializes without the addon"
        COMMAND wowai_overlay_component_test)
    set_tests_properties("WebView2 composition overlay initializes without the addon"
        PROPERTIES LABELS "component;overlay")

    include(Catch)
    catch_discover_tests(wowai_tests)

    add_custom_target(verify
        COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" -C $<CONFIG> --output-on-failure
        COMMAND pwsh -NoProfile -File "${PROJECT_SOURCE_DIR}/scripts/verify-repository.ps1"
        DEPENDS wowai_companion wowai_protocol_mock wowai_tests wowai_overlay_component_test wowai_codex_host_bundle
        USES_TERMINAL
        COMMENT "Running C++ tests and repository checks"
    )
endif()
