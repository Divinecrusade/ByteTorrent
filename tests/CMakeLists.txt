find_package(GTest CONFIG REQUIRED)

include(GoogleTest)

add_module_test(bencode)
add_module_test(torrent)
