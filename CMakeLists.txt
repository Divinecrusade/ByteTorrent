cmake_minimum_required(VERSION 3.28)

project(ByteTorrent CXX)

if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

# Enable experimental module support
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "2182bf5c-ef0d-489a-91da-49dbc3090d2a")
set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/std:c++latest /experimental:module)
    set(CMAKE_VS_GLOBALS "BuildStlModules=true")
    set(CMAKE_CXX_STANDARD_LIBRARY_MODULES ON)

    add_compile_options(/MP)
    add_compile_options(/W4)
    add_compile_options(/permissive- /Zc:__cplusplus)
    add_compile_options($<$<CONFIG:Release>:/O2>)
    add_compile_options($<$<CONFIG:Debug>:/Od>)
    add_compile_options($<$<CONFIG:Debug>:/Zi>)
    add_compile_options($<$<CONFIG:Release>:/GL>)
    add_link_options($<$<CONFIG:Release>:/LTCG>)
    add_compile_options($<$<CONFIG:Debug>:/fsanitize=address>)
endif()

include(cmake/ModuleUtils.cmake)

add_module_library(bencode)

find_package(OpenSSL REQUIRED)
add_module_library(torrent)
target_link_libraries(torrent PRIVATE OpenSSL::Crypto)
target_link_libraries(torrent PRIVATE bencode)

add_subdirectory(tests)
