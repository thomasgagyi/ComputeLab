add_library(ComputeLabEx2Foundation STATIC
    Ex2Input.cpp
    Ex2Input.hpp
    Ex2Sha256.cpp
    Ex2Sha256.hpp
    Ex2SemanticTypes.hpp
)

target_compile_features(ComputeLabEx2Foundation PUBLIC cxx_std_20)

target_include_directories(ComputeLabEx2Foundation
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

add_library(ComputeLabEx2CpuOracles STATIC
    Ex2CpuA.cpp
    Ex2CpuB.cpp
    Ex2CpuC.cpp
    Ex2CpuD.cpp
    Ex2CpuE.cpp
    Ex2CpuOracles.hpp
)

target_compile_features(ComputeLabEx2CpuOracles PUBLIC cxx_std_20)

target_include_directories(ComputeLabEx2CpuOracles
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ComputeLabEx2CpuOracles
    PUBLIC
        ComputeLabEx2Foundation
)

add_library(ComputeLabEx2Configuration STATIC
    Ex2Configuration.cpp
    Ex2Configuration.hpp
)

target_compile_features(ComputeLabEx2Configuration PUBLIC cxx_std_20)

target_include_directories(ComputeLabEx2Configuration
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ComputeLabEx2Configuration
    PUBLIC
        ComputeLabEx2Foundation
)

add_library(ComputeLabEx2Evidence STATIC
    Ex2Evidence.cpp
    Ex2Evidence.hpp
)

target_compile_features(ComputeLabEx2Evidence PUBLIC cxx_std_20)

target_include_directories(ComputeLabEx2Evidence
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(ComputeLabEx2Evidence
    PUBLIC
        ComputeLabEx2Configuration
        ComputeLabResults
)

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

add_library(ComputeLabEx2Gate0Support STATIC
    Ex2Gate0.cpp
    Ex2Gate0.hpp
    Ex2Gate0Supervisor.cpp
    Ex2Gate0Supervisor.hpp
)

target_compile_features(ComputeLabEx2Gate0Support PUBLIC cxx_std_20)
target_include_directories(ComputeLabEx2Gate0Support PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ComputeLabEx2Gate0Support
    PUBLIC
        ComputeLabResults
        ComputeLabEx2Foundation
)
