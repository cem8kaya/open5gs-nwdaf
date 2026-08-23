cmake_minimum_required(VERSION 3.22)
project(open5gs-nwdaf VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# P0-4: Hardening flags and warnings as errors
add_compile_options(-Wall -Wextra -Werror -O2 -D_FORTIFY_SOURCE=2 -fstack-protector-strong)
if(NOT APPLE)
    add_compile_options(-fPIE)
    add_link_options(-pie -Wl,-z,relro,-z,now)
endif()

# Suppress FetchContent timestamp policy warning
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

option(NWDAF_USE_SD_JOURNAL       "Use libsystemd sd-journal API (requires systemd dev headers)" ON)
option(NWDAF_BUILD_TESTS          "Build unit and integration tests" ON)
option(NWDAF_ENABLE_PUSH_DELIVERY "Enable subscription push delivery background thread (COMP-01)" ON)
# ARCH-05: TLS / mTLS for SBI interface (TS 33.501 §13.3).
# Requires OpenSSL dev headers; cpp-httplib uses CPPHTTPLIB_OPENSSL_SUPPORT.
option(NWDAF_USE_TLS              "Enable TLS for SBI (requires OpenSSL)" ON)

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
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    FetchContent_Declare(yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG        0.8.0)
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(yaml-cpp)
    message(STATUS "yaml-cpp fetched via FetchContent")
endif()

# spdlog
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.13.0)
    FetchContent_MakeAvailable(spdlog)
    message(STATUS "spdlog fetched via FetchContent")
endif()

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

# ARCH-05: OpenSSL (optional) — TLS for SBI interface
if(NWDAF_USE_TLS)
    find_package(OpenSSL REQUIRED)
    add_compile_definitions(NWDAF_USE_TLS)
    message(STATUS "TLS enabled — SBI will use httplib::SSLServer (OpenSSL)")
endif()

# SQLite3 (optional) — PROD-01 throughput persistence + PROD-02 subscription persistence
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    add_compile_definitions(NWDAF_HAS_SQLITE)
    message(STATUS "SQLite3 found — throughput history and subscription persistence enabled")
else()
    pkg_check_modules(SQLITE3 QUIET sqlite3)
    if(SQLITE3_FOUND)
        add_compile_definitions(NWDAF_HAS_SQLITE)
        message(STATUS "SQLite3 (pkg-config) found — persistence enabled")
    else()
        message(STATUS "SQLite3 not found — history_backend=sqlite will be a no-op (install libsqlite3-dev)")
    endif()
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
    src/ml/mos_estimator.cpp
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

if(NWDAF_USE_TLS)
    target_compile_definitions(nwdaf_lib PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(nwdaf_lib PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

if(mongocxx_FOUND)
    target_link_libraries(nwdaf_lib PUBLIC mongo::mongocxx_shared)
endif()

if(SQLite3_FOUND)
    target_link_libraries(nwdaf_lib PUBLIC SQLite::SQLite3)
elseif(SQLITE3_FOUND)
    target_link_libraries(nwdaf_lib PUBLIC ${SQLITE3_LIBRARIES})
    target_include_directories(nwdaf_lib PUBLIC ${SQLITE3_INCLUDE_DIRS})
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
