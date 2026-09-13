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

if(Q0_ENABLE_CUDA AND Q0_ENABLE_VULKAN)
    if(NOT TARGET CUDA::nvml)
        message(FATAL_ERROR "The configured CUDA Toolkit does not provide the CUDA::nvml target required by A4b.")
    endif()

    if(NOT DEFINED COMPUTELAB_CONFIGURE_PRESET OR COMPUTELAB_CONFIGURE_PRESET STREQUAL "")
        message(FATAL_ERROR "COMPUTELAB_CONFIGURE_PRESET must be supplied by the active configure preset.")
    endif()

    if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
        message(FATAL_ERROR "A4b build metadata requires the approved Ninja configure presets.")
    endif()

    execute_process(
        COMMAND "${CMAKE_MAKE_PROGRAM}" --version
        RESULT_VARIABLE computelab_ninja_version_result
        OUTPUT_VARIABLE COMPUTELAB_NINJA_VERSION
        ERROR_VARIABLE computelab_ninja_version_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT computelab_ninja_version_result EQUAL 0 OR COMPUTELAB_NINJA_VERSION STREQUAL "")
        message(FATAL_ERROR "Unable to acquire Ninja version during configuration: ${computelab_ninja_version_error}")
    endif()
endif()

add_subdirectory(src/input)
add_subdirectory(src/oracle)
add_subdirectory(src/results)
add_subdirectory(src/timing)
if(Q0_ENABLE_CUDA AND Q0_ENABLE_VULKAN)
    add_subdirectory(src/environment)
endif()
add_subdirectory(src/app)
add_subdirectory(tests)
add_subdirectory(tools/smoke)
