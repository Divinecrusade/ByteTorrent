find_package(GTest CONFIG REQUIRED)

add_executable()

target_link_libraries(
    GTest::gtest
    GTest::gtest_main
)

enable_testing()
add_test()