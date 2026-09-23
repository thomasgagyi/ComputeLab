add_executable(
    ComputeLab
    main.cpp
)

target_compile_features(
    ComputeLab
    PRIVATE
        cxx_std_20
)

if(TARGET ComputeLabEx1Support
    AND TARGET ComputeLabCudaTransform
    AND TARGET ComputeLabVulkanTransform)
    add_executable(
        ComputeLabEx1
        Ex1Main.cpp
    )

    target_compile_features(ComputeLabEx1 PRIVATE cxx_std_20)

    target_compile_definitions(ComputeLabEx1 PRIVATE
        COMPUTELAB_EX1_SPIRV_PATH="${COMPUTELAB_EX1_SPIRV_PATH}"
        COMPUTELAB_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
    )

    target_link_libraries(ComputeLabEx1
        PRIVATE
            ComputeLabEx1Support
            ComputeLabSeededInput
            ComputeLabDeterministicOracle
            ComputeLabHostTiming
            ComputeLabCudaTransform
            ComputeLabVulkanTransform
    )
endif()

if(TARGET ComputeLabEx2CudaA1
    AND TARGET ComputeLabEx2VulkanA1
    AND TARGET ComputeLabEx2CudaA2
    AND TARGET ComputeLabEx2VulkanA2
    AND TARGET ComputeLabEx2CpuOracles
    AND TARGET ComputeLabEx2Evidence
    AND TARGET ComputeLabEnvironment)
    add_library(
        ComputeLabEx2A1Integration
        STATIC
        Ex2A1Integration.cpp
        Ex2A1Integration.hpp
        Ex2A2Integration.cpp
        Ex2A2Integration.hpp
    )
    target_compile_features(ComputeLabEx2A1Integration PUBLIC cxx_std_20)
    target_include_directories(ComputeLabEx2A1Integration
        PUBLIC
            ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(ComputeLabEx2A1Integration
        PUBLIC
            ComputeLabEx2Foundation
            ComputeLabEx2CpuOracles
            ComputeLabEx2Configuration
            ComputeLabEx2Evidence
            ComputeLabEx2CudaA1
            ComputeLabEx2VulkanA1
            ComputeLabEx2CudaA2
            ComputeLabEx2VulkanA2
            ComputeLabEnvironment
    )

    add_executable(
        ComputeLabEx2A1
        Ex2A1Main.cpp
    )
    target_compile_features(ComputeLabEx2A1 PRIVATE cxx_std_20)
    target_compile_definitions(ComputeLabEx2A1 PRIVATE
        COMPUTELAB_EX2_A1_SPIRV_PATH="${COMPUTELAB_EX2_A1_SPIRV_PATH}"
        COMPUTELAB_EX2_A2_SPIRV_PATH="${COMPUTELAB_EX2_A2_SPIRV_PATH}"
        COMPUTELAB_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
    )
    target_link_libraries(ComputeLabEx2A1
        PRIVATE
            ComputeLabEx2A1Integration
    )
endif()

if(TARGET ComputeLabEx2CudaB
    AND TARGET ComputeLabEx2VulkanB
    AND TARGET ComputeLabEx2CpuOracles
    AND TARGET ComputeLabEx2Configuration)
    add_library(
        ComputeLabEx2BIntegration
        STATIC
        Ex2BIntegration.cpp
        Ex2BIntegration.hpp
    )
    target_compile_features(ComputeLabEx2BIntegration PUBLIC cxx_std_20)
    target_include_directories(ComputeLabEx2BIntegration
        PUBLIC
            ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(ComputeLabEx2BIntegration
        PUBLIC
            ComputeLabEx2Foundation
            ComputeLabEx2CpuOracles
            ComputeLabEx2Configuration
            ComputeLabEx2CudaB
            ComputeLabEx2VulkanB
    )
endif()

if(TARGET ComputeLabEx2Gate0Support
    AND TARGET ComputeLabCudaQualification
    AND TARGET ComputeLabVulkanQualification
    AND TARGET ComputeLabEnvironment)
    add_executable(
        ComputeLabEx2Gate0
        Ex2Gate0Main.cpp
    )

    target_compile_features(ComputeLabEx2Gate0 PRIVATE cxx_std_20)
    target_compile_definitions(ComputeLabEx2Gate0 PRIVATE
        COMPUTELAB_EX1_SPIRV_PATH="${COMPUTELAB_EX1_SPIRV_PATH}"
        COMPUTELAB_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
    )
    target_link_libraries(ComputeLabEx2Gate0
        PRIVATE
            ComputeLabEx2Gate0Support
            ComputeLabSeededInput
            ComputeLabDeterministicOracle
            ComputeLabCudaQualification
            ComputeLabVulkanQualification
            ComputeLabEnvironment
    )

    add_executable(
        ComputeLabEx2Gate0Supervisor
        Ex2Gate0SupervisorMain.cpp
    )
    target_compile_features(ComputeLabEx2Gate0Supervisor PRIVATE cxx_std_20)
    target_compile_definitions(ComputeLabEx2Gate0Supervisor PRIVATE
        COMPUTELAB_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
    )
    target_link_libraries(ComputeLabEx2Gate0Supervisor
        PRIVATE
            ComputeLabEx2Gate0Support
            ComputeLabEnvironment
    )
endif()
