cmake_minimum_required(VERSION 3.20)
project(ea_FRD CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

add_executable(float_inf_walk
    src/float_inf_walk.cpp
    src/lang.cpp
    src/format_util.cpp
    src/drive.cpp
    src/platform.cpp
)

target_include_directories(float_inf_walk PRIVATE include)

find_package(Threads REQUIRED)
target_link_libraries(float_inf_walk PRIVATE Threads::Threads)

if(WIN32)
    target_link_libraries(float_inf_walk PRIVATE winmm)
endif()

if(MINGW)
    target_link_options(float_inf_walk PRIVATE -static -static-libgcc -static-libstdc++)
endif()
