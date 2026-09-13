find_package(GTest CONFIG REQUIRED)

add_executable(ComputeLabTests
    SmokeTests.cpp
    SeededInputTests.cpp
    DeterministicTransformTests.cpp
)

target_link_libraries(ComputeLabTests
    PRIVATE
        GTest::gtest_main
        ComputeLabSeededInput
        ComputeLabDeterministicOracle
)

include(GoogleTest)

gtest_discover_tests(ComputeLabTests)
