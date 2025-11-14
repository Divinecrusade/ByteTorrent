find_package(GTest CONFIG REQUIRED)

add_executable(bencode_test bencode_test.cpp)

target_link_libraries(bencode_test 
    PRIVATE 
        bencode
        GTest::gtest
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(bencode_test)