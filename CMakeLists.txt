cmake_minimum_required(VERSION 3.20)
project(ea_FRDT CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

add_executable(ea-FRDT
    src/float_inf_walk.cpp
    src/lang.cpp
    src/format_util.cpp
    src/drive.cpp
    src/platform.cpp
)

target_include_directories(ea-FRDT PRIVATE include)

find_package(Threads REQUIRED)
target_link_libraries(ea-FRDT PRIVATE Threads::Threads)

if(WIN32)
    target_link_libraries(ea-FRDT PRIVATE winmm)
endif()

if(MINGW)
    target_link_options(ea-FRDT PRIVATE -static -static-libgcc -static-libstdc++)
endif()
