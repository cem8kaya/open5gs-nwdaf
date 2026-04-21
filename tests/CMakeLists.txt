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

add_executable(nwdaf_tests
    test_collector.cpp
    test_analytics.cpp
    test_server_integration.cpp
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

include(CTest)
include(Catch)
catch_discover_tests(nwdaf_tests)
