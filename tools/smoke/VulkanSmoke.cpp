#include <vulkan/vulkan.h>

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    std::string FormatUuid(const uint8_t (&uuid)[VK_UUID_SIZE])
    {
        std::ostringstream stream;
        stream << std::hex << std::setfill('0');

        for (uint8_t byte : uuid)
        {
            stream << std::setw(2) << static_cast<unsigned int>(byte);
        }

        return stream.str();
    }

    bool CreateComputeDevice(
        VkPhysicalDevice physicalDevice,
        uint32_t queueFamilyIndex)
    {
        constexpr float queuePriority = 1.0F;

        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        VkDeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;

        VkDevice logicalDevice = VK_NULL_HANDLE;
        const VkResult createResult =
            vkCreateDevice(
                physicalDevice,
                &deviceCreateInfo,
                nullptr,
                &logicalDevice);

        if (createResult != VK_SUCCESS)
        {
            std::cerr
                << "vkCreateDevice failed: "
                << createResult
                << '\n';

            return false;
        }

        VkQueue queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(
            logicalDevice,
            queueFamilyIndex,
            0,
            &queue);

        const bool queueCreated = queue != VK_NULL_HANDLE;
        vkDestroyDevice(logicalDevice, nullptr);

        if (!queueCreated)
        {
            std::cerr << "vkGetDeviceQueue returned a null queue.\n";
        }

        return queueCreated;
    }
}

int main()
{
    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "ComputeLab Vulkan Smoke";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    applicationInfo.pEngineName = "None";
    applicationInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceCreateInfo{};
    instanceCreateInfo.sType =
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pApplicationInfo = &applicationInfo;

    VkInstance instance = VK_NULL_HANDLE;

    const VkResult createResult =
        vkCreateInstance(
            &instanceCreateInfo,
            nullptr,
            &instance);

    if (createResult != VK_SUCCESS)
    {
        std::cerr
            << "vkCreateInstance failed: "
            << createResult
            << '\n';

        return EXIT_FAILURE;
    }

    uint32_t deviceCount = 0;

    VkResult result =
        vkEnumeratePhysicalDevices(
            instance,
            &deviceCount,
            nullptr);

    if (result != VK_SUCCESS || deviceCount == 0)
    {
        std::cerr << "No Vulkan physical devices found.\n";
        vkDestroyInstance(instance, nullptr);
        return EXIT_FAILURE;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);

    result =
        vkEnumeratePhysicalDevices(
            instance,
            &deviceCount,
            devices.data());

    if (result != VK_SUCCESS)
    {
        std::cerr
            << "vkEnumeratePhysicalDevices failed: "
            << result
            << '\n';

        vkDestroyInstance(instance, nullptr);
        return EXIT_FAILURE;
    }

    bool qualifiedDeviceFound = false;

    for (VkPhysicalDevice device : devices)
    {
        VkPhysicalDeviceIDProperties idProperties{};
        idProperties.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &idProperties;

        vkGetPhysicalDeviceProperties2(
            device,
            &properties);

        uint32_t queueFamilyCount = 0;

        vkGetPhysicalDeviceQueueFamilyProperties(
            device,
            &queueFamilyCount,
            nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(
            queueFamilyCount);

        vkGetPhysicalDeviceQueueFamilyProperties(
            device,
            &queueFamilyCount,
            queueFamilies.data());

        uint32_t qualifiedQueueFamily =
            std::numeric_limits<uint32_t>::max();

        for (uint32_t index = 0; index < queueFamilyCount; ++index)
        {
            const VkQueueFamilyProperties& queueFamily = queueFamilies[index];

            if (
                (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0 &&
                queueFamily.timestampValidBits != 0)
            {
                qualifiedQueueFamily = index;
                break;
            }
        }

        const bool hasComputeQueue =
            qualifiedQueueFamily != std::numeric_limits<uint32_t>::max();

        std::cout
            << "Vulkan device: "
            << properties.properties.deviceName
            << '\n'
            << "API version: "
            << VK_VERSION_MAJOR(properties.properties.apiVersion)
            << '.'
            << VK_VERSION_MINOR(properties.properties.apiVersion)
            << '.'
            << VK_VERSION_PATCH(properties.properties.apiVersion)
            << '\n'
            << "Compute queue: "
            << (hasComputeQueue ? "yes" : "no")
            << '\n';

        if (!hasComputeQueue)
        {
            std::cout << "Device timestamps: no\n";
            continue;
        }

        const uint32_t timestampValidBits =
            queueFamilies[qualifiedQueueFamily].timestampValidBits;

        std::cout
            << "Device timestamps: yes ("
            << timestampValidBits
            << " valid bits)\n";

        if (!CreateComputeDevice(device, qualifiedQueueFamily))
        {
            continue;
        }

        std::cout
            << "Logical device and compute queue: OK\n"
            << "COMPUTELAB_DEVICE_UUID="
            << FormatUuid(idProperties.deviceUUID)
            << '\n';

        qualifiedDeviceFound = true;
    }

    vkDestroyInstance(instance, nullptr);

    if (!qualifiedDeviceFound)
    {
        std::cerr
            << "No Vulkan device with compute timestamps and a usable queue "
            << "was found.\n";

        return EXIT_FAILURE;
    }

    std::cout << "Vulkan environment smoke: OK\n";

    return EXIT_SUCCESS;
}
