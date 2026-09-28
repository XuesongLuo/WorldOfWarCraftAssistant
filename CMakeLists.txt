cmake_minimum_required(VERSION 3.28)

project(
    WorldOfWarcraftAssistant
    VERSION 0.1.0
    DESCRIPTION "A non-invasive World of Warcraft information assistant"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "WorldOfWarcraftAssistant currently supports Windows only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WOWAI_BUILD_TESTS "Build C++ tests" ON)

if(WOWAI_BUILD_TESTS)
    include(CTest)
    enable_testing()
endif()

if(MSVC)
    # External targets such as Catch2 can compile sources in parallel into one PDB.
    # /FS serializes PDB writes and prevents MSVC C1041 failures.
    add_compile_options(/FS)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/companion/cmake")
include(WowAIWarnings)

add_subdirectory(companion)
