find_package(GTest CONFIG REQUIRED)

add_executable(ComputeLabTests
    SmokeTests.cpp
    SeededInputTests.cpp
    DeterministicTransformTests.cpp
    ResultRecordsTests.cpp
    HostTimingTests.cpp
    Ex2HostTimingTests.cpp
    Ex2Gate0Tests.cpp
    Ex2Gate0SupervisorTests.cpp
)

target_link_libraries(ComputeLabTests
    PRIVATE
        GTest::gtest_main
        ComputeLabSeededInput
        ComputeLabDeterministicOracle
        ComputeLabResults
        ComputeLabHostTiming
        ComputeLabEx2HostTiming
        ComputeLabEx2Gate0Support
)

if(TARGET ComputeLabEnvironment)
    target_sources(ComputeLabTests PRIVATE EnvironmentCollectorTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabEnvironment)
    target_compile_definitions(ComputeLabTests PRIVATE
        COMPUTELAB_TEST_CONFIGURE_PRESET="${COMPUTELAB_CONFIGURE_PRESET}"
        COMPUTELAB_TEST_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    )
endif()

if(TARGET ComputeLabEx1Support)
    target_sources(ComputeLabTests PRIVATE Ex1SupportTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabEx1Support)
endif()

if(TARGET ComputeLabCudaTransform)
    target_sources(ComputeLabTests PRIVATE CudaTransformTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabCudaTransform CUDA::cudart)
endif()

if(TARGET ComputeLabCudaQualification)
    target_sources(ComputeLabTests PRIVATE CudaQualificationTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabCudaQualification)
endif()

include(GoogleTest)

if(TARGET ComputeLabVulkanTransform)
    target_sources(ComputeLabTests PRIVATE VulkanTransformTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabVulkanTransform)
    target_compile_definitions(ComputeLabTests PRIVATE
        COMPUTELAB_EX1_SPIRV_PATH="${COMPUTELAB_EX1_SPIRV_PATH}")
endif()

if(TARGET ComputeLabVulkanQualification)
    target_sources(ComputeLabTests PRIVATE VulkanQualificationTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabVulkanQualification)
    target_compile_definitions(ComputeLabTests PRIVATE
        COMPUTELAB_EX1_SPIRV_PATH="${COMPUTELAB_EX1_SPIRV_PATH}")
endif()

if(TARGET ComputeLabEx2Gate0)
    target_sources(ComputeLabTests PRIVATE Ex2Gate0ChildSmokeTests.cpp)
    add_dependencies(ComputeLabTests ComputeLabEx2Gate0)
    target_compile_definitions(ComputeLabTests PRIVATE
        COMPUTELAB_EX2_GATE0_EXE="$<TARGET_FILE:ComputeLabEx2Gate0>"
        COMPUTELAB_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
    )
endif()

add_executable(ComputeLabEx2Gate0SupervisorTestChild
    Ex2Gate0SupervisorTestChild.cpp
)
target_compile_features(ComputeLabEx2Gate0SupervisorTestChild PRIVATE cxx_std_20)
target_link_libraries(ComputeLabEx2Gate0SupervisorTestChild
    PRIVATE ComputeLabEx2Gate0Support)
add_dependencies(ComputeLabTests ComputeLabEx2Gate0SupervisorTestChild)
target_compile_definitions(ComputeLabTests PRIVATE
    COMPUTELAB_EX2_GATE0_SUPERVISOR_TEST_CHILD="$<TARGET_FILE:ComputeLabEx2Gate0SupervisorTestChild>"
)

gtest_discover_tests(ComputeLabTests)
