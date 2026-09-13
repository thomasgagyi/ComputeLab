#include <cuda_runtime.h>

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <device_launch_parameters.h>
#include <sstream>
#include <string>

namespace
{
    bool CheckCuda(cudaError_t result, const char* operation)
    {
        if (result == cudaSuccess)
        {
            return true;
        }

        std::cerr
            << operation
            << " failed: "
            << cudaGetErrorString(result)
            << '\n';

        return false;
    }

    std::string FormatUuid(const cudaUUID_t& uuid)
    {
        std::ostringstream stream;
        stream << std::hex << std::setfill('0');

        for (char byte : uuid.bytes)
        {
            stream << std::setw(2)
                   << static_cast<unsigned int>(
                          static_cast<unsigned char>(byte));
        }

        return stream.str();
    }

    __global__ void WriteValue(int* value)
    {
        if (blockIdx.x == 0 && threadIdx.x == 0)
        {
            *value = 42;
        }
    }
}

int main()
{
    int deviceCount = 0;

    if (!CheckCuda(
        cudaGetDeviceCount(&deviceCount),
        "cudaGetDeviceCount"))
    {
        return EXIT_FAILURE;
    }

    if (deviceCount <= 0)
    {
        std::cerr << "No CUDA devices found.\n";
        return EXIT_FAILURE;
    }

    cudaDeviceProp properties{};

    if (!CheckCuda(
        cudaGetDeviceProperties(&properties, 0),
        "cudaGetDeviceProperties"))
    {
        return EXIT_FAILURE;
    }

    std::cout
        << "CUDA device: "
        << properties.name
        << '\n'
        << "Compute capability: "
        << properties.major
        << '.'
        << properties.minor
        << '\n'
        << "COMPUTELAB_DEVICE_UUID="
        << FormatUuid(properties.uuid)
        << '\n';

    int* deviceValue = nullptr;

    if (!CheckCuda(
        cudaMalloc(&deviceValue, sizeof(int)),
        "cudaMalloc"))
    {
        return EXIT_FAILURE;
    }

    WriteValue <<<1, 1 >>> (deviceValue);

    if (!CheckCuda(
        cudaGetLastError(),
        "CUDA kernel launch"))
    {
        cudaFree(deviceValue);
        return EXIT_FAILURE;
    }

    if (!CheckCuda(
        cudaDeviceSynchronize(),
        "cudaDeviceSynchronize"))
    {
        cudaFree(deviceValue);
        return EXIT_FAILURE;
    }

    int hostValue = 0;

    if (!CheckCuda(
        cudaMemcpy(
            &hostValue,
            deviceValue,
            sizeof(int),
            cudaMemcpyDeviceToHost),
        "cudaMemcpy"))
    {
        cudaFree(deviceValue);
        return EXIT_FAILURE;
    }

    cudaFree(deviceValue);

    if (hostValue != 42)
    {
        std::cerr
            << "CUDA smoke result mismatch: "
            << hostValue
            << '\n';

        return EXIT_FAILURE;
    }

    std::cout << "CUDA kernel execution: OK\n";

    return EXIT_SUCCESS;
}
