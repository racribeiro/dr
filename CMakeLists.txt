cmake_minimum_required(VERSION 3.20)
project(dji_neo_sdk VERSION 0.1.0 LANGUAGES C)

option(DJI_NEO_BUILD_TESTS "Build SDK tests" ON)
option(DJI_NEO_BUILD_JAVA "Build optional JNI library and Java API" OFF)

add_library(dji_neo
    src/dji_neo.c
    src/duml.c
    src/commands.c
)
add_library(dji_neo::dji_neo ALIAS dji_neo)
target_compile_features(dji_neo PUBLIC c_std_11)
target_include_directories(dji_neo
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<INSTALL_INTERFACE:include>
)
set_target_properties(dji_neo PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(dji_neo PRIVATE -Wall -Wextra -Wpedantic)
if(UNIX)
    target_link_libraries(dji_neo PUBLIC m)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS dji_neo EXPORT dji_neoTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/dji_neo DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT dji_neoTargets NAMESPACE dji_neo::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dji_neo)
configure_package_config_file(cmake/dji_neoConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/dji_neoConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dji_neo)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/dji_neoConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/dji_neoConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/dji_neoConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dji_neo)

if(DJI_NEO_BUILD_TESTS)
    enable_testing()
    foreach(suite sdk commands activation activation_profile capture telemetry signing)
        add_executable(test_${suite} tests/test_${suite}.c)
        target_link_libraries(test_${suite} PRIVATE dji_neo)
        target_compile_options(test_${suite} PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
        add_test(NAME ${suite} COMMAND test_${suite})
    endforeach()
    add_test(NAME udp_loopback COMMAND test_commands --udp)
    set_tests_properties(udp_loopback PROPERTIES SKIP_RETURN_CODE 77)
    target_compile_definitions(test_capture PRIVATE
        CAPTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/reference/captures/gimbalmove-sanitized.pcap")
    target_compile_definitions(test_activation_profile PRIVATE
        PROFILE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/reference/activation/app-profile-55.txt")
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME activation_generated COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/reference/tools/generate_activation.py
            --check ${CMAKE_CURRENT_SOURCE_DIR}/src/activation_frames.inc)
    endif()
    add_executable(test_realframes reference/tests/test_realframes.c)
    target_include_directories(test_realframes PRIVATE src)
    target_link_libraries(test_realframes PRIVATE dji_neo)
    target_compile_options(test_realframes PRIVATE -UNDEBUG)
    add_test(NAME realframes COMMAND test_realframes)
endif()

if(DJI_NEO_BUILD_JAVA)
    add_subdirectory(bindings/java)
endif()
