find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.4)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    message(STATUS "Catch2 fetched via FetchContent")
endif()

add_library(nwdaf_mock
    mock_open5gs.cpp
)
target_include_directories(nwdaf_mock PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(nwdaf_mock PUBLIC nwdaf_lib)

# Unit + ARCH tests — no shared network resources, safe to run in parallel.
add_executable(nwdaf_tests
    test_collector.cpp
    test_analytics.cpp
    test_arch_improvements.cpp   # TIER-4 ARCH-01…ARCH-05 tests
)
target_include_directories(nwdaf_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(nwdaf_tests PRIVATE
    nwdaf_mock
    nwdaf_lib
    Catch2::Catch2WithMain
)

# Integration tests bind to port 17779 and must not run in parallel with each
# other.  RESOURCE_LOCK ensures CTest serialises every test in this target.
add_executable(nwdaf_integration_tests
    test_server_integration.cpp
)
target_include_directories(nwdaf_integration_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(nwdaf_integration_tests PRIVATE
    nwdaf_mock
    nwdaf_lib
    Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(nwdaf_tests)
catch_discover_tests(nwdaf_integration_tests
    PROPERTIES RESOURCE_LOCK "nwdaf_port_17779"
)
