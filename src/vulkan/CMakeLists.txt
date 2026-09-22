set(COMPUTELAB_EX1_SPIRV_PATH "${CMAKE_CURRENT_BINARY_DIR}/Ex1Transform.comp.spv")
add_custom_command(
    OUTPUT "${COMPUTELAB_EX1_SPIRV_PATH}"
    # SPIR-V 1.3 retains literal LocalSize, avoiding an unrelated maintenance4
    # feature requirement from SPIR-V 1.6 LocalSizeId emission.
    COMMAND Vulkan::glslc --target-env=vulkan1.3 --target-spv=spv1.3
        "${CMAKE_CURRENT_SOURCE_DIR}/Ex1Transform.comp"
        -o "${COMPUTELAB_EX1_SPIRV_PATH}"
    DEPENDS Ex1Transform.comp
    VERBATIM
)
add_library(ComputeLabVulkanTransform STATIC
    VulkanTransform.cpp VulkanTransform.hpp
    VulkanTiming.cpp VulkanTiming.hpp
    "${COMPUTELAB_EX1_SPIRV_PATH}"
)
target_compile_features(ComputeLabVulkanTransform PUBLIC cxx_std_20)
target_include_directories(ComputeLabVulkanTransform PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(ComputeLabVulkanTransform PUBLIC Vulkan::Vulkan)

add_library(ComputeLabVulkanQualification STATIC
    VulkanQualification.cpp VulkanQualification.hpp
    "${COMPUTELAB_EX1_SPIRV_PATH}"
)
target_compile_features(ComputeLabVulkanQualification PUBLIC cxx_std_20)
target_include_directories(ComputeLabVulkanQualification PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(ComputeLabVulkanQualification
    PUBLIC
        Vulkan::Vulkan
        ComputeLabEx2HostTiming
    PRIVATE
        ComputeLabDeterministicOracle
)
set(COMPUTELAB_EX1_SPIRV_PATH "${COMPUTELAB_EX1_SPIRV_PATH}" PARENT_SCOPE)

set(COMPUTELAB_EX2_A1_SPIRV_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/Ex2A1.comp.spv")
add_custom_command(
    OUTPUT "${COMPUTELAB_EX2_A1_SPIRV_PATH}"
    COMMAND Vulkan::glslc --target-env=vulkan1.3 --target-spv=spv1.3
        "${CMAKE_CURRENT_SOURCE_DIR}/Ex2A1.comp"
        -o "${COMPUTELAB_EX2_A1_SPIRV_PATH}"
    DEPENDS Ex2A1.comp
    VERBATIM
)

add_library(ComputeLabEx2VulkanA1 STATIC
    Ex2VulkanA1.cpp
    Ex2VulkanA1.hpp
    "${COMPUTELAB_EX2_A1_SPIRV_PATH}"
)
target_compile_features(ComputeLabEx2VulkanA1 PUBLIC cxx_std_20)
target_include_directories(ComputeLabEx2VulkanA1
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(ComputeLabEx2VulkanA1
    PUBLIC
        Vulkan::Vulkan
        ComputeLabEx2Configuration
)
set(COMPUTELAB_EX2_A1_SPIRV_PATH
    "${COMPUTELAB_EX2_A1_SPIRV_PATH}" PARENT_SCOPE)

set(COMPUTELAB_EX2_A2_SPIRV_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/Ex2A2.comp.spv")
add_custom_command(
    OUTPUT "${COMPUTELAB_EX2_A2_SPIRV_PATH}"
    COMMAND Vulkan::glslc --target-env=vulkan1.3 --target-spv=spv1.3
        "${CMAKE_CURRENT_SOURCE_DIR}/Ex2A2.comp"
        -o "${COMPUTELAB_EX2_A2_SPIRV_PATH}"
    DEPENDS Ex2A2.comp
    VERBATIM
)

add_library(ComputeLabEx2VulkanA2 STATIC
    Ex2VulkanA2.cpp
    Ex2VulkanA2.hpp
    "${COMPUTELAB_EX2_A2_SPIRV_PATH}"
)
target_compile_features(ComputeLabEx2VulkanA2 PUBLIC cxx_std_20)
target_include_directories(ComputeLabEx2VulkanA2
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(ComputeLabEx2VulkanA2
    PUBLIC
        ComputeLabEx2VulkanA1
)
set(COMPUTELAB_EX2_A2_SPIRV_PATH
    "${COMPUTELAB_EX2_A2_SPIRV_PATH}" PARENT_SCOPE)
