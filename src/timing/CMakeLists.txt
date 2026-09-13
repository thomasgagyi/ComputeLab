add_library(ComputeLabHostTiming STATIC
    HostTiming.cpp
    HostTiming.hpp
)

target_compile_features(ComputeLabHostTiming PUBLIC cxx_std_20)

target_include_directories(ComputeLabHostTiming
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
