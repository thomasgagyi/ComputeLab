if(NOT WIN32)
    message(FATAL_ERROR "A4b environment metadata collection is currently Windows-specific.")
endif()

add_library(ComputeLabEnvironment STATIC
    EnvironmentCollector.cpp
    EnvironmentCollector.hpp
)

target_compile_features(ComputeLabEnvironment PUBLIC cxx_std_20)

target_include_directories(ComputeLabEnvironment
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_compile_definitions(ComputeLabEnvironment
    PRIVATE
        COMPUTELAB_BUILD_COMPILER_NAME="${CMAKE_CXX_COMPILER_ID}"
        COMPUTELAB_BUILD_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
        COMPUTELAB_BUILD_CMAKE_VERSION="${CMAKE_VERSION}"
        COMPUTELAB_BUILD_NINJA_VERSION="${COMPUTELAB_NINJA_VERSION}"
        COMPUTELAB_BUILD_CONFIGURE_PRESET="${COMPUTELAB_CONFIGURE_PRESET}"
        COMPUTELAB_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
        COMPUTELAB_BUILD_CUDA_TOOLKIT_VERSION="${CUDAToolkit_VERSION}"
        COMPUTELAB_BUILD_VULKAN_SDK_VERSION="${Vulkan_VERSION}"
)

target_link_libraries(ComputeLabEnvironment
    PUBLIC
        ComputeLabResults
    PRIVATE
        CUDA::cudart
        CUDA::nvml
        Vulkan::Vulkan
        Advapi32
)
