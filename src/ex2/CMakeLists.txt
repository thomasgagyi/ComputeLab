add_library(ComputeLabEx2HostTiming STATIC
    Ex2HostTiming.cpp
    Ex2HostTiming.hpp
)

target_compile_features(ComputeLabEx2HostTiming PUBLIC cxx_std_20)

target_include_directories(ComputeLabEx2HostTiming
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ComputeLabEx2HostTiming
    PUBLIC
        ComputeLabHostTiming
)
