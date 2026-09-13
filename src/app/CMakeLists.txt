add_executable(
    ComputeLab
    main.cpp
)

target_compile_features(
    ComputeLab
    PRIVATE
        cxx_std_20
)