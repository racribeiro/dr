# Keep Java/JNI opt-in: a C-only or Android host does not need a desktop JDK.
add_library(dji_neo_jni SHARED jni/dji_neo_jni.c)
target_link_libraries(dji_neo_jni PRIVATE dji_neo::dji_neo)
target_compile_options(dji_neo_jni PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(NOT ANDROID)
    find_package(Java 11 REQUIRED COMPONENTS Development Runtime)
    # Resolve the JDK behind /usr/bin/javac before FindJNI probes its headers.
    get_filename_component(_javac_real "${Java_JAVAC_EXECUTABLE}" REALPATH)
    get_filename_component(_java_bin "${_javac_real}" DIRECTORY)
    get_filename_component(JAVA_HOME "${_java_bin}" DIRECTORY)
    # Headers only: do not require AWT or libjvm to build a loadable JNI bridge.
    find_path(_jni_include jni.h PATHS "${JAVA_HOME}/include" NO_DEFAULT_PATH REQUIRED)
    find_path(_jni_platform jni_md.h PATHS "${JAVA_HOME}/include"
        PATH_SUFFIXES linux darwin win32 NO_DEFAULT_PATH REQUIRED)
    target_include_directories(dji_neo_jni PRIVATE ${_jni_include} ${_jni_platform})
    # No link to libjvm: the host JVM loads this library, not the reverse.
endif()
install(TARGETS dji_neo_jni
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_DATADIR}/dji_neo/java)
install(FILES consumer-rules.pro README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/dji_neo/java)

if(NOT ANDROID)
    set(_java_sources
        ${CMAKE_CURRENT_SOURCE_DIR}/src/io/github/racribeiro/djineo/NativeNeo.java
        ${CMAKE_CURRENT_SOURCE_DIR}/src/io/github/racribeiro/djineo/Commands.java
        ${CMAKE_CURRENT_SOURCE_DIR}/src/io/github/racribeiro/djineo/NeoClient.java)
    set(_classes ${CMAKE_CURRENT_BINARY_DIR}/classes)
    set(_jar ${CMAKE_CURRENT_BINARY_DIR}/dji-neo.jar)
    add_custom_command(OUTPUT ${_jar}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_classes}
        COMMAND ${Java_JAVAC_EXECUTABLE} --release 8 -Xlint:all,-options -Werror -d ${_classes} ${_java_sources}
        COMMAND ${Java_JAR_EXECUTABLE} cf ${_jar} -C ${_classes} .
        DEPENDS ${_java_sources} VERBATIM)
    add_custom_target(dji_neo_java ALL DEPENDS ${_jar})
    install(FILES ${_jar} DESTINATION ${CMAKE_INSTALL_DATADIR}/dji_neo)
    if(DJI_NEO_BUILD_TESTS)
        set(_test_classes ${CMAKE_CURRENT_BINARY_DIR}/test-classes)
        set(_test_class ${_test_classes}/io/github/racribeiro/djineo/TestJava.class)
        add_custom_command(OUTPUT ${_test_class}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_test_classes}
            COMMAND ${Java_JAVAC_EXECUTABLE} --release 8 -Xlint:all,-options -Werror -cp ${_jar}
                -d ${_test_classes} ${CMAKE_CURRENT_SOURCE_DIR}/tests/TestJava.java
            DEPENDS ${_jar} tests/TestJava.java VERBATIM)
        add_custom_target(test_java_classes ALL DEPENDS ${_test_class})
        add_dependencies(test_java_classes dji_neo_java)
        if(WIN32)
            set(_classpath "${_jar}\;${_test_classes}")
        else()
            set(_classpath "${_jar}:${_test_classes}")
        endif()
        add_test(NAME java_jni COMMAND ${Java_JAVA_EXECUTABLE} -Xcheck:jni -ea
            "-Djava.library.path=$<TARGET_FILE_DIR:dji_neo_jni>" -cp "${_classpath}"
            io.github.racribeiro.djineo.TestJava ${CMAKE_CURRENT_SOURCE_DIR}/../..)
        # -Xcheck:jni emits warnings without failing the JVM, so make them failures.
        set_tests_properties(java_jni PROPERTIES FAIL_REGULAR_EXPRESSION
            "WARNING in native method;FATAL ERROR in native method")
    endif()
endif()
