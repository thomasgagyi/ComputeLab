find_package(GTest CONFIG REQUIRED)

add_executable(ComputeLabTests
    SmokeTests.cpp
    SeededInputTests.cpp
    DeterministicTransformTests.cpp
    ResultRecordsTests.cpp
    HostTimingTests.cpp
)

target_link_libraries(ComputeLabTests
    PRIVATE
        GTest::gtest_main
        ComputeLabSeededInput
        ComputeLabDeterministicOracle
        ComputeLabResults
        ComputeLabHostTiming
)

if(TARGET ComputeLabEnvironment)
    target_sources(ComputeLabTests PRIVATE EnvironmentCollectorTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabEnvironment)
endif()

if(TARGET ComputeLabCudaTransform)
    target_sources(ComputeLabTests PRIVATE CudaTransformTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabCudaTransform)
endif()

include(GoogleTest)

gtest_discover_tests(ComputeLabTests)
