find_package(GTest CONFIG REQUIRED)

add_executable(ComputeLabTests
    SmokeTests.cpp
)

target_link_libraries(ComputeLabTests
    PRIVATE
        GTest::gtest_main
)

include(GoogleTest)

gtest_discover_tests(ComputeLabTests)