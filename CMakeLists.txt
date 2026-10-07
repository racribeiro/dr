cmake_minimum_required(VERSION 3.20)
project(dji_neo_sdk VERSION 0.1.0 LANGUAGES C)

option(DJI_NEO_BUILD_TESTS "Build SDK tests" ON)

add_library(dji_neo STATIC
    src/dji_neo.c
    src/duml.c
)
add_library(dji_neo::dji_neo ALIAS dji_neo)
target_compile_features(dji_neo PUBLIC c_std_11)
target_include_directories(dji_neo
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_options(dji_neo PRIVATE -Wall -Wextra -Wpedantic)
if(UNIX)
    target_link_libraries(dji_neo PUBLIC m)
endif()

if(DJI_NEO_BUILD_TESTS)
    enable_testing()
    add_executable(dji_neo_tests tests/test_sdk.c)
    target_link_libraries(dji_neo_tests PRIVATE dji_neo)
    add_test(NAME dji_neo_tests COMMAND dji_neo_tests)
endif()
