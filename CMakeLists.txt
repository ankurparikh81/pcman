cmake_minimum_required(VERSION 3.16)
project(pcman LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core game library (shared between executable and unit tests)
set(CORE_SOURCES
    src/cga_renderer.cpp
    src/pc_speaker.cpp
    src/maze.cpp
    src/entities.cpp
    src/game.cpp
)

add_library(pcman_core STATIC ${CORE_SOURCES})

target_include_directories(pcman_core PUBLIC
    src
    $<$<PLATFORM_ID:Darwin>:/opt/homebrew/include /usr/local/include>
)

target_link_directories(pcman_core PUBLIC
    $<$<PLATFORM_ID:Darwin>:/opt/homebrew/lib /usr/local/lib>
)

find_package(raylib QUIET)

if(raylib_FOUND)
    target_link_libraries(pcman_core PUBLIC raylib)
else()
    target_link_libraries(pcman_core PUBLIC raylib)
    if(APPLE)
        target_link_libraries(pcman_core PUBLIC
            "-framework OpenGL"
            "-framework Cocoa"
            "-framework IOKit"
            "-framework CoreVideo"
        )
    elseif(UNIX AND NOT APPLE)
        target_link_libraries(pcman_core PUBLIC GL m pthread dl rt X11)
    elseif(WIN32)
        target_link_libraries(pcman_core PUBLIC opengl32 gdi32 winmm)
    endif()
endif()

add_executable(pcman src/main.cpp)
target_link_libraries(pcman PRIVATE pcman_core)

enable_testing()
add_executable(test_pcman tests/test_pcman.cpp)
target_link_libraries(test_pcman PRIVATE pcman_core)
add_test(NAME test_pcman COMMAND test_pcman)
