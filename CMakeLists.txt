cmake_minimum_required(VERSION 3.20)
project(hound VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(HOUND_BUILD_TESTS "Build unit/integration tests" ON)
option(HOUND_BUILD_BENCH "Build benchmarks" ON)

include(FetchContent)

set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1
)
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.18.3
)
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)

FetchContent_MakeAvailable(Catch2 httplib json)

find_package(Threads REQUIRED)

add_library(hound_core INTERFACE)
target_include_directories(hound_core INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(hound_core INTERFACE
  httplib::httplib
  nlohmann_json::nlohmann_json
  Threads::Threads
)

add_executable(hound src/main.cpp)
target_link_libraries(hound PRIVATE hound_core)

add_executable(hound_bulk_load tools/bulk_load.cpp)
target_link_libraries(hound_bulk_load PRIVATE hound_core)

if(HOUND_BUILD_TESTS)
  enable_testing()
  add_executable(hound_tests
    tests/unit/test_normalizer.cpp
    tests/unit/test_trie.cpp
    tests/unit/test_bk_tree.cpp
    tests/unit/test_fuzzy_index.cpp
    tests/unit/test_score_merger.cpp
    tests/unit/test_bulk_loader.cpp
    tests/unit/test_snapshot.cpp
    tests/integration/test_http_api.cpp
  )
  target_link_libraries(hound_tests PRIVATE hound_core Catch2::Catch2WithMain)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(CTest)
  include(Catch)
  catch_discover_tests(hound_tests)
endif()

if(HOUND_BUILD_BENCH)
  add_executable(hound_bench
    benchmarks/bench_main.cpp
    benchmarks/synth_generator/synth.cpp
  )
  target_include_directories(hound_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
  )
  target_link_libraries(hound_bench PRIVATE hound_core)
endif()

install(TARGETS hound RUNTIME DESTINATION bin)
