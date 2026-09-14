add_library(ComputeLabEx1Support STATIC
    Ex1Support.cpp
    Ex1Support.hpp
)

target_compile_features(ComputeLabEx1Support PUBLIC cxx_std_20)

target_include_directories(ComputeLabEx1Support
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ComputeLabEx1Support
    PUBLIC
        ComputeLabEnvironment
        ComputeLabResults
)
