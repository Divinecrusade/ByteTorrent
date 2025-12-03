find_package(GTest CONFIG REQUIRED)

include(GoogleTest)

add_module_test(bencode)
add_module_test(torrent)
add_module_test(peer_wire)
add_module_test(piece_manager)
add_module_test(peer_manager)
add_module_test(tracker)
