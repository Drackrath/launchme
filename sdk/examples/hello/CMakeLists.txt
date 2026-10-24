cmake_minimum_required(VERSION 3.24)
project(launchme_hello_plugin LANGUAGES C)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This example requires Windows x64")
endif()
add_library(hello SHARED plugin.c)
target_include_directories(hello PRIVATE ../..)
set_property(TARGET hello PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_custom_target(package ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json" "$<TARGET_FILE_DIR:hello>/plugin.json"
    COMMAND ${CMAKE_COMMAND} -E tar cf "$<TARGET_FILE_DIR:hello>/example.hello.zip" --format=zip -- plugin.json hello.dll
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:hello>"
    DEPENDS hello "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json" VERBATIM)
