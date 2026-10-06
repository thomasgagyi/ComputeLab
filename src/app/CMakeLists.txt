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

if(TARGET ComputeLabEx2CudaC
    AND TARGET ComputeLabEx2VulkanC
    AND TARGET ComputeLabEx2CpuOracles
    AND TARGET ComputeLabEx2Configuration)
    add_library(
        ComputeLabEx2CIntegration
        STATIC
        Ex2CIntegration.cpp
        Ex2CIntegration.hpp
    )
    target_compile_features(ComputeLabEx2CIntegration PUBLIC cxx_std_20)
    target_include_directories(ComputeLabEx2CIntegration
        PUBLIC
            ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(ComputeLabEx2CIntegration
        PUBLIC
            ComputeLabEx2Foundation
            ComputeLabEx2CpuOracles
            ComputeLabEx2Configuration
            ComputeLabEx2CudaC
            ComputeLabEx2VulkanC
    )
endif()

if(TARGET ComputeLabEx2CudaD1
    AND TARGET ComputeLabEx2VulkanD1
    AND TARGET ComputeLabEx2CpuOracles
    AND TARGET ComputeLabEx2Configuration)
    add_library(
        ComputeLabEx2D1Integration
        STATIC
        Ex2D1Integration.cpp
        Ex2D1Integration.hpp
    )
    target_compile_features(ComputeLabEx2D1Integration PUBLIC cxx_std_20)
    target_include_directories(ComputeLabEx2D1Integration
        PUBLIC
            ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(ComputeLabEx2D1Integration
        PUBLIC
            ComputeLabEx2Foundation
            ComputeLabEx2CpuOracles
            ComputeLabEx2Configuration
            ComputeLabEx2CudaD1
            ComputeLabEx2VulkanD1
    )
endif()

if(TARGET ComputeLabEx2CudaE
    AND TARGET ComputeLabEx2VulkanE
    AND TARGET ComputeLabEx2CpuOracles
    AND TARGET ComputeLabEx2Configuration
    AND TARGET ComputeLabEx2HostTiming)
    add_library(
        ComputeLabEx2EIntegration
        STATIC
        Ex2EIntegration.cpp
        Ex2EIntegration.hpp
    )
    target_compile_features(ComputeLabEx2EIntegration PUBLIC cxx_std_20)
    target_include_directories(ComputeLabEx2EIntegration
        PUBLIC
            ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(ComputeLabEx2EIntegration
        PUBLIC
            ComputeLabEx2Foundation
            ComputeLabEx2CpuOracles
            ComputeLabEx2Configuration
            ComputeLabEx2HostTiming
            ComputeLabEx2CudaE
            ComputeLabEx2VulkanE
    )
endif()

if(TARGET ComputeLabEx2A1Integration
    AND TARGET ComputeLabEx2BIntegration
    AND TARGET ComputeLabEx2CIntegration
    AND TARGET ComputeLabEx2D1Integration
    AND TARGET ComputeLabEx2EIntegration
    AND TARGET ComputeLabEnvironment)
    add_library(
        ComputeLabEx2CorrectnessExecution
        STATIC
        Ex2CorrectnessExecution.cpp
        Ex2CorrectnessExecution.hpp
    )
    target_compile_features(ComputeLabEx2CorrectnessExecution PUBLIC cxx_std_20)
    target_include_directories(ComputeLabEx2CorrectnessExecution
        PUBLIC ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(ComputeLabEx2CorrectnessExecution
        PUBLIC
            ComputeLabEx2A1Integration
            ComputeLabEx2BIntegration
            ComputeLabEx2CIntegration
            ComputeLabEx2D1Integration
            ComputeLabEx2EIntegration
            ComputeLabEx2Evidence
            ComputeLabEnvironment
    )

    add_executable(
        ComputeLabEx2Correctness
        Ex2CorrectnessMain.cpp
    )
    target_compile_features(ComputeLabEx2Correctness PRIVATE cxx_std_20)
    target_compile_definitions(ComputeLabEx2Correctness PRIVATE
        COMPUTELAB_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
        COMPUTELAB_EX2_A1_SPIRV_PATH="${COMPUTELAB_EX2_A1_SPIRV_PATH}"
        COMPUTELAB_EX2_A2_SPIRV_PATH="${COMPUTELAB_EX2_A2_SPIRV_PATH}"
        COMPUTELAB_EX2_B1_SPIRV_PATH="${COMPUTELAB_EX2_B1_SPIRV_PATH}"
        COMPUTELAB_EX2_B2_SPIRV_PATH="${COMPUTELAB_EX2_B2_SPIRV_PATH}"
        COMPUTELAB_EX2_C_SPIRV_PATH="${COMPUTELAB_EX2_C_SPIRV_PATH}"
        COMPUTELAB_EX2_D1_SPIRV_PATH="${COMPUTELAB_EX2_D1_SPIRV_PATH}"
    )
    target_link_libraries(ComputeLabEx2Correctness
        PRIVATE ComputeLabEx2CorrectnessExecution ComputeLabEx2CorrectnessControl)

    add_executable(ComputeLabEx2CorrectnessSupervisor Ex2CorrectnessSupervisorMain.cpp)
    target_compile_features(ComputeLabEx2CorrectnessSupervisor PRIVATE cxx_std_20)
    target_compile_definitions(ComputeLabEx2CorrectnessSupervisor PRIVATE
        COMPUTELAB_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
        COMPUTELAB_EX2_A1_SPIRV_PATH="${COMPUTELAB_EX2_A1_SPIRV_PATH}"
        COMPUTELAB_EX2_A2_SPIRV_PATH="${COMPUTELAB_EX2_A2_SPIRV_PATH}"
        COMPUTELAB_EX2_B1_SPIRV_PATH="${COMPUTELAB_EX2_B1_SPIRV_PATH}"
        COMPUTELAB_EX2_B2_SPIRV_PATH="${COMPUTELAB_EX2_B2_SPIRV_PATH}"
        COMPUTELAB_EX2_C_SPIRV_PATH="${COMPUTELAB_EX2_C_SPIRV_PATH}"
        COMPUTELAB_EX2_D1_SPIRV_PATH="${COMPUTELAB_EX2_D1_SPIRV_PATH}")
    target_link_libraries(ComputeLabEx2CorrectnessSupervisor PRIVATE ComputeLabEx2CorrectnessControl)
endif()

if(TARGET ComputeLabEx2CudaA1 AND TARGET ComputeLabEx2VulkanA1
    AND TARGET ComputeLabEx2CudaD1 AND TARGET ComputeLabEx2VulkanD1
    AND TARGET ComputeLabEx2Stage5Evidence AND TARGET ComputeLabEnvironment)
    add_library(ComputeLabEx2Stage5Execution STATIC Ex2Stage5Execution.cpp Ex2Stage5Execution.hpp)
    target_compile_features(ComputeLabEx2Stage5Execution PUBLIC cxx_std_20)
    target_include_directories(ComputeLabEx2Stage5Execution PUBLIC ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(ComputeLabEx2Stage5Execution PUBLIC
        ComputeLabEx2Stage5Evidence ComputeLabEx2HostTiming ComputeLabEnvironment ComputeLabEx2Stage5Control
        ComputeLabEx2CudaA1 ComputeLabEx2VulkanA1 ComputeLabEx2CudaD1 ComputeLabEx2VulkanD1)
    add_executable(ComputeLabEx2Stage5 Ex2Stage5Main.cpp)
    target_compile_features(ComputeLabEx2Stage5 PRIVATE cxx_std_20)
    target_compile_definitions(ComputeLabEx2Stage5 PRIVATE
        COMPUTELAB_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
        COMPUTELAB_EX2_A1_SPIRV_PATH="${COMPUTELAB_EX2_A1_SPIRV_PATH}"
        COMPUTELAB_EX2_D1_SPIRV_PATH="${COMPUTELAB_EX2_D1_SPIRV_PATH}")
    target_link_libraries(ComputeLabEx2Stage5 PRIVATE ComputeLabEx2Stage5Execution)
    add_executable(ComputeLabEx2Stage5Supervisor Ex2Stage5SupervisorMain.cpp)
    target_compile_features(ComputeLabEx2Stage5Supervisor PRIVATE cxx_std_20)
    target_compile_definitions(ComputeLabEx2Stage5Supervisor PRIVATE
        COMPUTELAB_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
        COMPUTELAB_EX2_A1_SPIRV_PATH="${COMPUTELAB_EX2_A1_SPIRV_PATH}"
        COMPUTELAB_EX2_D1_SPIRV_PATH="${COMPUTELAB_EX2_D1_SPIRV_PATH}")
    target_link_libraries(ComputeLabEx2Stage5Supervisor PRIVATE ComputeLabEx2Stage5Control)
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

if(TARGET ComputeLabEx2Stage6Evidence AND TARGET ComputeLabEnvironment
    AND TARGET ComputeLabEx2CudaA2 AND TARGET ComputeLabEx2VulkanA2
    AND TARGET ComputeLabEx2CudaB AND TARGET ComputeLabEx2VulkanB
    AND TARGET ComputeLabEx2CudaC AND TARGET ComputeLabEx2VulkanC
    AND TARGET ComputeLabEx2CudaD1 AND TARGET ComputeLabEx2VulkanD1
    AND TARGET ComputeLabEx2CudaE AND TARGET ComputeLabEx2VulkanE)
    add_library(ComputeLabEx2Stage6Execution STATIC Ex2Stage6Execution.cpp Ex2Stage6Execution.hpp)
    target_compile_features(ComputeLabEx2Stage6Execution PUBLIC cxx_std_20)
    target_include_directories(ComputeLabEx2Stage6Execution PUBLIC ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(ComputeLabEx2Stage6Execution PUBLIC
        ComputeLabEx2Stage6Evidence ComputeLabEx2CpuOracles ComputeLabEx2HostTiming ComputeLabEnvironment
        ComputeLabEx2CudaA1 ComputeLabEx2VulkanA1 ComputeLabEx2CudaA2 ComputeLabEx2VulkanA2
        ComputeLabEx2CudaB ComputeLabEx2VulkanB ComputeLabEx2CudaC ComputeLabEx2VulkanC
        ComputeLabEx2CudaD1 ComputeLabEx2VulkanD1 ComputeLabEx2CudaE ComputeLabEx2VulkanE)
    add_executable(ComputeLabEx2Stage6 Ex2Stage6Main.cpp)
    target_compile_features(ComputeLabEx2Stage6 PRIVATE cxx_std_20)
    target_link_libraries(ComputeLabEx2Stage6 PRIVATE ComputeLabEx2Stage6Execution ComputeLabEx2Stage6Progress)
    target_compile_definitions(ComputeLabEx2Stage6 PRIVATE
        COMPUTELAB_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
        COMPUTELAB_EX2_A1_SPIRV_PATH="${COMPUTELAB_EX2_A1_SPIRV_PATH}"
        COMPUTELAB_EX2_A2_SPIRV_PATH="${COMPUTELAB_EX2_A2_SPIRV_PATH}"
        COMPUTELAB_EX2_B1_SPIRV_PATH="${COMPUTELAB_EX2_B1_SPIRV_PATH}"
        COMPUTELAB_EX2_B2_SPIRV_PATH="${COMPUTELAB_EX2_B2_SPIRV_PATH}"
        COMPUTELAB_EX2_C_SPIRV_PATH="${COMPUTELAB_EX2_C_SPIRV_PATH}"
        COMPUTELAB_EX2_D1_SPIRV_PATH="${COMPUTELAB_EX2_D1_SPIRV_PATH}")
endif()

if(TARGET ComputeLabEx2Stage6Control)
    add_executable(ComputeLabEx2Stage6Supervisor Ex2Stage6SupervisorMain.cpp)
    target_compile_features(ComputeLabEx2Stage6Supervisor PRIVATE cxx_std_20)
    target_link_libraries(ComputeLabEx2Stage6Supervisor PRIVATE ComputeLabEx2Stage6Control)
    target_compile_definitions(ComputeLabEx2Stage6Supervisor PRIVATE
        COMPUTELAB_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}"
        COMPUTELAB_EX2_A1_SPIRV_PATH="${COMPUTELAB_EX2_A1_SPIRV_PATH}"
        COMPUTELAB_EX2_A2_SPIRV_PATH="${COMPUTELAB_EX2_A2_SPIRV_PATH}"
        COMPUTELAB_EX2_B1_SPIRV_PATH="${COMPUTELAB_EX2_B1_SPIRV_PATH}"
        COMPUTELAB_EX2_B2_SPIRV_PATH="${COMPUTELAB_EX2_B2_SPIRV_PATH}"
        COMPUTELAB_EX2_C_SPIRV_PATH="${COMPUTELAB_EX2_C_SPIRV_PATH}"
        COMPUTELAB_EX2_D1_SPIRV_PATH="${COMPUTELAB_EX2_D1_SPIRV_PATH}")
endif()
