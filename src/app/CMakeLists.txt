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
