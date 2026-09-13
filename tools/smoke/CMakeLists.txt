if(Q0_ENABLE_CUDA)
    add_executable(
        ComputeLabCudaSmoke
        CudaSmoke.cu
    )

    target_link_libraries(
        ComputeLabCudaSmoke
        PRIVATE
            CUDA::cudart
    )

    set_target_properties(
        ComputeLabCudaSmoke
        PROPERTIES
            CUDA_ARCHITECTURES native
            CUDA_STANDARD 20
            CUDA_STANDARD_REQUIRED ON
    )

    add_test(
        NAME ComputeLabSmoke.CUDA
        COMMAND ComputeLabCudaSmoke
    )

    set_tests_properties(
        ComputeLabSmoke.CUDA
        PROPERTIES
            LABELS "smoke;cuda"
    )
endif()


if(Q0_ENABLE_VULKAN)
    add_executable(
        ComputeLabVulkanSmoke
        VulkanSmoke.cpp
    )

    target_compile_features(
        ComputeLabVulkanSmoke
        PRIVATE
            cxx_std_20
    )

    target_link_libraries(
        ComputeLabVulkanSmoke
        PRIVATE
            Vulkan::Vulkan
    )

    add_test(
        NAME ComputeLabSmoke.Vulkan
        COMMAND ComputeLabVulkanSmoke
    )

    set_tests_properties(
        ComputeLabSmoke.Vulkan
        PROPERTIES
            LABELS "smoke;vulkan"
    )
endif()


if(Q0_ENABLE_CUDA AND Q0_ENABLE_VULKAN)
    add_test(
        NAME ComputeLabSmoke.GpuIdentity
        COMMAND
            powershell
            -NoProfile
            -ExecutionPolicy Bypass
            -File ${CMAKE_SOURCE_DIR}/scripts/check-gpu-identity.ps1
            -CudaExecutable $<TARGET_FILE:ComputeLabCudaSmoke>
            -VulkanExecutable $<TARGET_FILE:ComputeLabVulkanSmoke>
    )

    set_tests_properties(
        ComputeLabSmoke.GpuIdentity
        PROPERTIES
            LABELS "smoke;cuda;vulkan"
    )
endif()
