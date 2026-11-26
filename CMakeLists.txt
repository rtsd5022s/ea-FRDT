cmake_minimum_required(VERSION 3.20)
project(ea_FRDT CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# ---------------------------------------------------------------------------
# Optional shared core: libeafrdt.dll + a small DLL-linked host executable.
# The standalone ea-FRDT target above is intentionally untouched; it still
# builds the full CLI with its own engine and no runtime DLL dependency.
# ---------------------------------------------------------------------------
option(EAFRDT_BUILD_SHARED "Build libeafrdt.dll and DLL-linked host/test" ON)

if(EAFRDT_BUILD_SHARED)
    add_library(eafrdt SHARED
        libeafrdt/eafrdt.cpp
        libeafrdt/c_api.cpp
    )

    target_include_directories(eafrdt
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/libeafrdt
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_definitions(eafrdt PRIVATE EAFRDT_BUILD_DLL)
    target_link_libraries(eafrdt PRIVATE Threads::Threads)

    set_target_properties(eafrdt PROPERTIES
        OUTPUT_NAME "libeafrdt"
        PREFIX ""
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )

    if(MINGW)
        # Keep the DLL and both consumers free of libc++/libunwind runtime DLLs.
        target_link_options(eafrdt PRIVATE -static)
    endif()

    add_executable(eafrdt-cli libeafrdt/cli_main.cpp)
    target_link_libraries(eafrdt-cli PRIVATE eafrdt)
    target_compile_definitions(eafrdt-cli PRIVATE EAFRDT_USE_DLL)
    if(MINGW)
        target_link_options(eafrdt-cli PRIVATE -static)
    endif()

    add_executable(test_capi libeafrdt/test_capi.cpp)
    target_link_libraries(test_capi PRIVATE eafrdt)
    target_compile_definitions(test_capi PRIVATE EAFRDT_USE_DLL)
    if(MINGW)
        target_link_options(test_capi PRIVATE -static)
    endif()
endif()
