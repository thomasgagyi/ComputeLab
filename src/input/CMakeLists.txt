add_library(ComputeLabSeededInput STATIC
    SeededInput.cpp
    SeededInput.hpp
)

target_compile_features(ComputeLabSeededInput PUBLIC cxx_std_20)

target_include_directories(ComputeLabSeededInput
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
