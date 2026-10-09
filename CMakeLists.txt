cmake_minimum_required(VERSION 3.16)
project(pcman LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(SOURCES
    src/main.cpp
    src/cga_renderer.cpp
    src/pc_speaker.cpp
    src/maze.cpp
    src/entities.cpp
    src/game.cpp
)

add_executable(pcman ${SOURCES})

target_include_directories(pcman PRIVATE
    src
    $<$<PLATFORM_ID:Darwin>:/opt/homebrew/include /usr/local/include>
)

target_link_directories(pcman PRIVATE
    $<$<PLATFORM_ID:Darwin>:/opt/homebrew/lib /usr/local/lib>
)

find_package(raylib QUIET)

if(raylib_FOUND)
    target_link_libraries(pcman PRIVATE raylib)
else()
    target_link_libraries(pcman PRIVATE raylib)
    if(APPLE)
        target_link_libraries(pcman PRIVATE
            "-framework OpenGL"
            "-framework Cocoa"
            "-framework IOKit"
            "-framework CoreVideo"
        )
    elseif(UNIX AND NOT APPLE)
        target_link_libraries(pcman PRIVATE GL m pthread dl rt X11)
    elseif(WIN32)
        target_link_libraries(pcman PRIVATE opengl32 gdi32 winmm)
    endif()
endif()
