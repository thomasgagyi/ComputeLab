#include <vulkan/vulkan.h>

#include <cstdlib>
#include <iostream>
#include <vector>

int main()
{
    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "ComputeLab Vulkan Smoke";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    applicationInfo.pEngineName = "None";
    applicationInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_0;

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

    bool computeDeviceFound = false;

    for (VkPhysicalDevice device : devices)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(
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

        bool supportsCompute = false;

        for (const auto& queueFamily : queueFamilies)
        {
            if ((queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0)
            {
                supportsCompute = true;
                break;
            }
        }

        std::cout
            << "Vulkan device: "
            << properties.deviceName
            << '\n'
            << "API version: "
            << VK_VERSION_MAJOR(properties.apiVersion)
            << '.'
            << VK_VERSION_MINOR(properties.apiVersion)
            << '.'
            << VK_VERSION_PATCH(properties.apiVersion)
            << '\n'
            << "Compute queue: "
            << (supportsCompute ? "yes" : "no")
            << '\n';

        if (supportsCompute)
        {
            computeDeviceFound = true;
        }
    }

    vkDestroyInstance(instance, nullptr);

    if (!computeDeviceFound)
    {
        std::cerr
            << "No Vulkan compute-capable device was found.\n";

        return EXIT_FAILURE;
    }

    std::cout << "Vulkan environment smoke: OK\n";

    return EXIT_SUCCESS;
}