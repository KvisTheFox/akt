cmake_minimum_required(VERSION 3.20)
project(akt C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} "${CMAKE_SOURCE_DIR}/bin")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg} "${CMAKE_SOURCE_DIR}/bin")
endforeach()

add_library(akt_runtime STATIC runtime/src/akt_runtime.c)
target_include_directories(akt_runtime PUBLIC runtime/include)
target_compile_definitions(akt_runtime PRIVATE AKT_ROOT="${CMAKE_SOURCE_DIR}")

add_executable(aktc
    aktc/src/main.c
    aktc/src/lexer.c
    aktc/src/parser.c
    aktc/src/checker.c
    aktc/src/codegen.c
    aktc/src/cimport.c
    aktc/src/util.c
)
target_include_directories(aktc PRIVATE aktc/src runtime/include)
target_link_libraries(aktc PRIVATE akt_runtime)
target_compile_definitions(aktc PRIVATE AKT_ROOT="${CMAKE_SOURCE_DIR}")

add_executable(katek katek/src/main.c)
target_compile_definitions(katek PRIVATE AKT_ROOT="${CMAKE_SOURCE_DIR}")
if(UNIX)
    target_link_libraries(katek PRIVATE m)
endif()

add_executable(akt_tests tests/c/runtime_test.c)
target_include_directories(akt_tests PRIVATE runtime/include)
target_link_libraries(akt_tests PRIVATE akt_runtime)

if(WIN32)
    add_executable(akt-setup installer/src/main.c)
    target_link_libraries(akt-setup PRIVATE advapi32 user32)
endif()

if(WIN32)
    target_compile_options(akt_runtime PRIVATE /W3)
else()
    target_compile_options(akt_runtime PRIVATE -Wall -Wextra)
endif()
