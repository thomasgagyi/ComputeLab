add_library(ComputeLabCudaTransform STATIC
    CudaTransform.cu
    CudaTransform.hpp
)

target_include_directories(ComputeLabCudaTransform
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ComputeLabCudaTransform
    PRIVATE
        CUDA::cudart
)

set_target_properties(ComputeLabCudaTransform
    PROPERTIES
        CUDA_ARCHITECTURES native
        CUDA_STANDARD 20
        CUDA_STANDARD_REQUIRED ON
)

target_compile_features(ComputeLabCudaTransform PUBLIC cxx_std_20)

add_library(ComputeLabCudaQualification STATIC
    CudaQualification.cu
    CudaQualification.hpp
)

target_include_directories(ComputeLabCudaQualification
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ComputeLabCudaQualification
    PUBLIC
        ComputeLabEx2HostTiming
    PRIVATE
        ComputeLabDeterministicOracle
        CUDA::cudart
)

set_target_properties(ComputeLabCudaQualification
    PROPERTIES
        CUDA_ARCHITECTURES native
        CUDA_STANDARD 20
        CUDA_STANDARD_REQUIRED ON
)

target_compile_features(ComputeLabCudaQualification PUBLIC cxx_std_20)

add_library(ComputeLabEx2CudaA1 STATIC
    Ex2CudaA1.cu
    Ex2CudaA1.hpp
)

target_include_directories(ComputeLabEx2CudaA1
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ComputeLabEx2CudaA1
    PUBLIC
        ComputeLabEx2Configuration
    PRIVATE
        CUDA::cudart
)

set_target_properties(ComputeLabEx2CudaA1
    PROPERTIES
        CUDA_ARCHITECTURES native
        CUDA_STANDARD 20
        CUDA_STANDARD_REQUIRED ON
)

target_compile_features(ComputeLabEx2CudaA1 PUBLIC cxx_std_20)
