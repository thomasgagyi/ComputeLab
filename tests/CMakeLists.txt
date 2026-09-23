find_package(GTest CONFIG REQUIRED)

add_executable(ComputeLabTests
    SmokeTests.cpp
    SeededInputTests.cpp
    DeterministicTransformTests.cpp
    ResultRecordsTests.cpp
    HostTimingTests.cpp
    Ex2HostTimingTests.cpp
    Ex2InputTests.cpp
    Ex2Sha256Tests.cpp
    Ex2CpuATests.cpp
    Ex2CpuBTests.cpp
    Ex2CpuCTests.cpp
    Ex2CpuDTests.cpp
    Ex2CpuETests.cpp
    Ex2ConfigurationTests.cpp
    Ex2EvidenceTests.cpp
    Ex2FoundationIntegrationTests.cpp
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
        ComputeLabEx2Foundation
        ComputeLabEx2CpuOracles
        ComputeLabEx2Configuration
        ComputeLabEx2Evidence
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

if(TARGET ComputeLabEx2CudaA1)
    target_sources(ComputeLabTests PRIVATE Ex2CudaA1Tests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabEx2CudaA1 CUDA::cudart)
endif()

if(TARGET ComputeLabEx2CudaA2)
    target_sources(ComputeLabTests PRIVATE Ex2CudaA2Tests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabEx2CudaA2 CUDA::cudart)
endif()

if(TARGET ComputeLabEx2CudaB)
    target_sources(ComputeLabTests PRIVATE Ex2CudaBTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabEx2CudaB CUDA::cudart)
endif()

if(TARGET ComputeLabEx2CudaC)
    target_sources(ComputeLabTests PRIVATE Ex2CudaCTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabEx2CudaC CUDA::cudart)
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

if(TARGET ComputeLabEx2VulkanA1)
    target_sources(ComputeLabTests PRIVATE Ex2VulkanA1Tests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabEx2VulkanA1)
    target_compile_definitions(ComputeLabTests PRIVATE
        COMPUTELAB_EX2_A1_SPIRV_PATH="${COMPUTELAB_EX2_A1_SPIRV_PATH}")
endif()

if(TARGET ComputeLabEx2VulkanA2)
    target_sources(ComputeLabTests PRIVATE Ex2VulkanA2Tests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabEx2VulkanA2)
    target_compile_definitions(ComputeLabTests PRIVATE
        COMPUTELAB_EX2_A2_SPIRV_PATH="${COMPUTELAB_EX2_A2_SPIRV_PATH}")
endif()

if(TARGET ComputeLabEx2VulkanB)
    target_sources(ComputeLabTests PRIVATE Ex2VulkanBTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabEx2VulkanB)
    target_compile_definitions(ComputeLabTests PRIVATE
        COMPUTELAB_EX2_B1_SPIRV_PATH="${COMPUTELAB_EX2_B1_SPIRV_PATH}"
        COMPUTELAB_EX2_B2_SPIRV_PATH="${COMPUTELAB_EX2_B2_SPIRV_PATH}")
endif()

if(TARGET ComputeLabEx2VulkanC)
    target_sources(ComputeLabTests PRIVATE Ex2VulkanCTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabEx2VulkanC)
    target_compile_definitions(ComputeLabTests PRIVATE
        COMPUTELAB_EX2_C_SPIRV_PATH="${COMPUTELAB_EX2_C_SPIRV_PATH}")
endif()

if(TARGET ComputeLabEx2A1Integration AND TARGET ComputeLabEx2A1)
    target_sources(ComputeLabTests PRIVATE
        Ex2A1IntegrationTests.cpp
        Ex2A2IntegrationTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabEx2A1Integration)
    add_dependencies(ComputeLabTests ComputeLabEx2A1)
    target_compile_definitions(ComputeLabTests PRIVATE
        COMPUTELAB_EX2_A1_SPIRV_PATH="${COMPUTELAB_EX2_A1_SPIRV_PATH}"
        COMPUTELAB_EX2_A2_SPIRV_PATH="${COMPUTELAB_EX2_A2_SPIRV_PATH}"
        COMPUTELAB_EX2_A1_EXE="$<TARGET_FILE:ComputeLabEx2A1>"
        COMPUTELAB_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
    )
endif()

if(TARGET ComputeLabEx2BIntegration)
    target_sources(ComputeLabTests PRIVATE Ex2BIntegrationTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabEx2BIntegration)
    target_compile_definitions(ComputeLabTests PRIVATE
        COMPUTELAB_EX2_B1_SPIRV_PATH="${COMPUTELAB_EX2_B1_SPIRV_PATH}"
        COMPUTELAB_EX2_B2_SPIRV_PATH="${COMPUTELAB_EX2_B2_SPIRV_PATH}")
endif()

if(TARGET ComputeLabEx2CIntegration)
    target_sources(ComputeLabTests PRIVATE Ex2CIntegrationTests.cpp)
    target_link_libraries(ComputeLabTests PRIVATE ComputeLabEx2CIntegration)
    target_compile_definitions(ComputeLabTests PRIVATE
        COMPUTELAB_EX2_C_SPIRV_PATH="${COMPUTELAB_EX2_C_SPIRV_PATH}")
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
