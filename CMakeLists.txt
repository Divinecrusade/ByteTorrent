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

add_module_library(torrent)
find_package(OpenSSL REQUIRED)
target_link_libraries(torrent PRIVATE OpenSSL::Crypto)
target_link_libraries(torrent PRIVATE bencode)

add_module_library(peer_wire)
find_package(asio CONFIG REQUIRED)
target_link_libraries(peer_wire PUBLIC asio::asio)
if(WIN32)
    target_link_libraries(peer_wire PUBLIC ws2_32 mswsock)
endif()
target_compile_definitions(peer_wire PUBLIC BYTE_TORRENT_HAS_ASIO)

add_module_library(piece_manager)
target_link_libraries(piece_manager PRIVATE torrent)
target_link_libraries(piece_manager PRIVATE peer_wire)

add_module_library(peer_manager)
target_link_libraries(peer_manager PRIVATE torrent)
target_link_libraries(peer_manager PRIVATE peer_wire)
target_link_libraries(peer_manager PRIVATE piece_manager)

add_module_library(tracker)
target_link_libraries(tracker PRIVATE bencode)
target_link_libraries(tracker PRIVATE peer_wire)

add_module_library(torrent_client)
target_link_libraries(torrent_client PUBLIC bencode)
target_link_libraries(torrent_client PUBLIC torrent)
target_link_libraries(torrent_client PUBLIC peer_wire)
target_link_libraries(torrent_client PUBLIC tracker)
target_link_libraries(torrent_client PUBLIC piece_manager)
target_link_libraries(torrent_client PUBLIC peer_manager)

add_subdirectory(tests)


add_executable(byte_torrent src/main.cpp)
find_package(asio CONFIG REQUIRED)
target_link_libraries(byte_torrent PRIVATE torrent_client)