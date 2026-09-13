add_executable(
    ComputeLab
    "ComputeLab.cpp"
    "ComputeLab.h"
)

target_compile_features(
    ComputeLab
    PRIVATE
        cxx_std_20
)