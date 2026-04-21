cmake_minimum_required(VERSION 3.22)
project(open5gs-nwdaf VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(NWDAF_USE_SD_JOURNAL      "Use libsystemd sd-journal API (requires systemd dev headers)" ON)
option(NWDAF_BUILD_TESTS         "Build unit and integration tests" ON)
option(NWDAF_ENABLE_PUSH_DELIVERY "Enable subscription push delivery background thread (COMP-01)" ON)

# ── Dependencies ─────────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)

include(FetchContent)

# cpp-httplib (header-only)
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.15.3)
FetchContent_MakeAvailable(httplib)

# nlohmann/json (header-only)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(json)

# yaml-cpp
find_package(yaml-cpp REQUIRED)

# spdlog
find_package(spdlog REQUIRED)

# libsystemd
if(NWDAF_USE_SD_JOURNAL)
    pkg_check_modules(SYSTEMD REQUIRED libsystemd)
    add_compile_definitions(NWDAF_USE_SD_JOURNAL)
endif()

# mongocxx (optional)
find_package(mongocxx QUIET)
if(mongocxx_FOUND)
    add_compile_definitions(NWDAF_HAS_MONGODB)
    message(STATUS "MongoDB C++ driver found — subscriber count enabled")
else()
    message(WARNING "MongoDB C++ driver not found — subscriber count disabled (returns 0)")
endif()

# ── NWDAF library ─────────────────────────────────────────────────────────────
set(NWDAF_LIB_SOURCES
    src/nwdaf_config.cpp
    src/nwdaf_collector.cpp
    src/nwdaf_analytics.cpp
    src/nwdaf_server.cpp
    src/nwdaf_subscription.cpp
    src/ml/isolation_forest.cpp
    src/ml/ewma_predictor.cpp
)

if(NWDAF_ENABLE_PUSH_DELIVERY)
    list(APPEND NWDAF_LIB_SOURCES src/nwdaf_notifier.cpp)
    add_compile_definitions(NWDAF_ENABLE_PUSH_DELIVERY)
    message(STATUS "Push delivery enabled (NWDAF_ENABLE_PUSH_DELIVERY=ON)")
endif()

add_library(nwdaf_lib ${NWDAF_LIB_SOURCES})

target_include_directories(nwdaf_lib PUBLIC include)
target_link_libraries(nwdaf_lib PUBLIC
    httplib::httplib
    nlohmann_json::nlohmann_json
    yaml-cpp
    spdlog::spdlog
)

if(NWDAF_USE_SD_JOURNAL)
    target_link_libraries(nwdaf_lib PUBLIC ${SYSTEMD_LIBRARIES})
    target_include_directories(nwdaf_lib PUBLIC ${SYSTEMD_INCLUDE_DIRS})
endif()

if(mongocxx_FOUND)
    target_link_libraries(nwdaf_lib PUBLIC mongo::mongocxx_shared)
endif()

# ── Main executable ───────────────────────────────────────────────────────────
add_executable(open5gs-nwdafd src/main.cpp)
target_link_libraries(open5gs-nwdafd PRIVATE nwdaf_lib)

install(TARGETS open5gs-nwdafd DESTINATION /usr/local/bin)
install(FILES config/nwdaf.yaml DESTINATION /etc/open5gs)
install(FILES systemd/open5gs-nwdafd.service DESTINATION /etc/systemd/system)

# ── Tests ──────────────────────────────────────────────────────────────────────
if(NWDAF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
