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
