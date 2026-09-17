find_package(GTest CONFIG REQUIRED)

add_executable(ComputeLabTests
    SmokeTests.cpp
    SeededInputTests.cpp
    DeterministicTransformTests.cpp
    ResultRecordsTests.cpp
    HostTimingTests.cpp
    Ex2HostTimingTests.cpp
)

target_link_libraries(ComputeLabTests
    PRIVATE
        GTest::gtest_main
        ComputeLabSeededInput
        ComputeLabDeterministicOracle
        ComputeLabResults
        ComputeLabHostTiming
        ComputeLabEx2HostTiming
)

if(TARGET ComputeLabEnvironment)
    target_sources(ComputeLabTests PRIVATE EnvironmentCollectorTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabEnvironment)
endif()

if(TARGET ComputeLabEx1Support)
    target_sources(ComputeLabTests PRIVATE Ex1SupportTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabEx1Support)
endif()

if(TARGET ComputeLabCudaTransform)
    target_sources(ComputeLabTests PRIVATE CudaTransformTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabCudaTransform CUDA::cudart)
endif()

include(GoogleTest)

if(TARGET ComputeLabVulkanTransform)
    target_sources(ComputeLabTests PRIVATE VulkanTransformTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabVulkanTransform)
    target_compile_definitions(ComputeLabTests PRIVATE
        COMPUTELAB_EX1_SPIRV_PATH="${COMPUTELAB_EX1_SPIRV_PATH}")
endif()

gtest_discover_tests(ComputeLabTests)
