add_library(ComputeLabDeterministicOracle STATIC
    DeterministicTransform.cpp
    DeterministicTransform.hpp
)

target_compile_features(ComputeLabDeterministicOracle PUBLIC cxx_std_20)

target_include_directories(ComputeLabDeterministicOracle
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
