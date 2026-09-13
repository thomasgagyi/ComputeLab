cmake_minimum_required(VERSION 3.25)

project(ComputeLab LANGUAGES CXX)

enable_testing()

option(Q0_ENABLE_CUDA "Enable CUDA support" ON)
option(Q0_ENABLE_VULKAN "Enable Vulkan support" ON)

if(Q0_ENABLE_CUDA)
    include(CheckLanguage)
    check_language(CUDA)

    if(NOT CMAKE_CUDA_COMPILER)
        message(FATAL_ERROR "Q0_ENABLE_CUDA is ON, but no CUDA compiler was found.")
    endif()

    enable_language(CUDA)
    find_package(CUDAToolkit 13.4 REQUIRED)
endif()

if(Q0_ENABLE_VULKAN)
    find_package(Vulkan 1.4 REQUIRED)
endif()

add_subdirectory(src/input)
add_subdirectory(src/oracle)
add_subdirectory(src/app)
add_subdirectory(tests)
add_subdirectory(tools/smoke)
