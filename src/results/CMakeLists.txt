add_library(ComputeLabResults STATIC
    ResultRecords.cpp
    ResultRecords.hpp
)

target_compile_features(ComputeLabResults PUBLIC cxx_std_20)

target_include_directories(ComputeLabResults
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
