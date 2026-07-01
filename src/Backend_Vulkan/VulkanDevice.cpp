#include "../Application/EngineApplication.h"

// TO-DO:
    // Mueva aquí las comprobaciones de soporte de presentación una vez que
    // se haya refactorizado la selección de la familia de queues.
    // La búsqueda de la familia de colas se duplica en createLogicalDevice().
    // Más adelante esto debería trasladarse a un elemento reutilizable
    // Función auxiliar findQueueFamilies().
bool EngineApplication::isDeviceSuitable(vk::raii::PhysicalDevice const& physicalDevice)
{
    auto queueFamilies = physicalDevice.getQueueFamilyProperties();

    bool supportsGraphics =
        std::ranges::any_of(
            queueFamilies,
            [](auto const& qfp)
            {
                return !!(
                    qfp.queueFlags &
                    vk::QueueFlagBits::eGraphics);
            });

    auto availableDeviceExtensions = physicalDevice.enumerateDeviceExtensionProperties();

    bool supportsAllRequiredExtensions =
        std::ranges::all_of(
            requiredDeviceExtension,
            [&availableDeviceExtensions]
            (auto const& requiredExtension)
            {
                return std::ranges::any_of(
                    availableDeviceExtensions,
                    [requiredExtension]
                    (auto const& availableExtension)
                    {
                        return strcmp(
                            availableExtension.extensionName,
                            requiredExtension) == 0;
                    });
            });

    auto features = physicalDevice.getFeatures();

    return
        supportsGraphics &&
        supportsAllRequiredExtensions &&
        features.samplerAnisotropy;
}

void EngineApplication::pickPhysicalDevice()
{
    std::vector<vk::raii::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();

    std::cout << "Available Vulkan GPUs\n";
    std::cout << "========================================\n";

    for (const auto& gpu : physicalDevices)
    {
        auto props = gpu.getProperties();
        std::cout << "GPU: " << props.deviceName << '\n';
    }

    auto const devIter = std::ranges::find_if(physicalDevices, [&](auto const& physicalDevice) { return isDeviceSuitable(physicalDevice); });
    if (devIter == physicalDevices.end())
    {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
    physicalDevice = *devIter;

    msaaSamples = getMaxUsableSampleCount();

    std::cout << "MSAA Samples: "
        << vk::to_string(msaaSamples)
        << '\n';
}

void EngineApplication::detectFeatureSupport()
{
    // Get device properties to check Vulkan version
    vk::PhysicalDeviceProperties deviceProperties = physicalDevice.getProperties();

    // Get available extensions
    std::vector<vk::ExtensionProperties> availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();

    // Check for dynamic rendering support
    if (deviceProperties.apiVersion >= VK_VERSION_1_3)
    {
        appInfo.dynamicRenderingSupported = true;
        std::cout << "Dynamic rendering supported via Vulkan 1.3\n";
    }
    else
    {
        // Check for the extension on older Vulkan versions
        for (const auto& extension : availableExtensions)
        {
            if (strcmp(extension.extensionName, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0)
            {
                appInfo.dynamicRenderingSupported = true;
                std::cout << "Dynamic rendering supported via extension\n";
                break;
            }
        }
    }

    // Check for timeline semaphores support
    if (deviceProperties.apiVersion >= VK_VERSION_1_2)
    {
        appInfo.timelineSemaphoresSupported = true;
        std::cout << "Timeline semaphores supported via Vulkan 1.2\n";
    }
    else
    {
        // Check for the extension on older Vulkan versions
        for (const auto& extension : availableExtensions)
        {
            if (strcmp(extension.extensionName, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0)
            {
                appInfo.timelineSemaphoresSupported = true;
                std::cout << "Timeline semaphores supported via extension\n";
                break;
            }
        }
    }

    // Check for synchronization2 support
    if (deviceProperties.apiVersion >= VK_VERSION_1_3)
    {
        appInfo.synchronization2Supported = true;
        std::cout << "Synchronization2 supported via Vulkan 1.3\n";
    }
    else
    {
        // Check for the extension on older Vulkan versions
        for (const auto& extension : availableExtensions)
        {
            if (strcmp(extension.extensionName, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) == 0)
            {
                appInfo.synchronization2Supported = true;
                std::cout << "Synchronization2 supported via extension\n";
                break;
            }
        }
    }

    auto featureSupport =
        physicalDevice.getFeatures2<
        vk::PhysicalDeviceFeatures2,
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();

    appInfo.extendedDynamicStateSupported =
        featureSupport
        .get<
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>()
        .extendedDynamicState;

    // Add required extensions based on feature support
    if (appInfo.dynamicRenderingSupported && deviceProperties.apiVersion < VK_VERSION_1_3)
    {
        requiredDeviceExtension.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    }

    if (appInfo.timelineSemaphoresSupported && deviceProperties.apiVersion < VK_VERSION_1_2)
    {
        requiredDeviceExtension.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
    }

    if (appInfo.synchronization2Supported && deviceProperties.apiVersion < VK_VERSION_1_3)
    {
        requiredDeviceExtension.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    }
}

void EngineApplication::printSelectedGPU()
{
    auto props = physicalDevice.getProperties();

    std::cout << "\n";
    std::cout << "Selected GPU\n";
    std::cout << "========================================\n";

    std::cout << "Name: " << props.deviceName << "\n";
    std::cout << "API Version: " << VK_VERSION_MAJOR(props.apiVersion) << "." << VK_VERSION_MINOR(props.apiVersion) << "." << VK_VERSION_PATCH(props.apiVersion) << "\n\n";

    auto features =
        physicalDevice.getFeatures2<
        vk::PhysicalDeviceFeatures2,
        vk::PhysicalDeviceVulkan11Features
        >();

    std::cout
        << "shaderDrawParameters = "
        << features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters
        << '\n';
}

// ------------------------------------------------------
// Crea el dispositivo lógico.
// Flujo:
//
// 1. Buscar una familia de colas(queues) compatible con gráficos.
// 2. Solicitar las características modernas que utilizaremos.
// 3. Habilitar las extensiones necesarias.
// 4. Crear el dispositivo lógico.
// 5. Obtener la cola gráfica.
//
// Después de esta función ya podemos enviar trabajo a la GPU.
void EngineApplication::createLogicalDevice()
{
    auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

    constexpr uint32_t InvalidQueueIndex = UINT32_MAX;
    uint32_t queueIndex = InvalidQueueIndex;

    for (uint32_t qfpIndex = 0;
        qfpIndex < queueFamilyProperties.size();
        qfpIndex++)
    {
        if ((queueFamilyProperties[qfpIndex].queueFlags &
            vk::QueueFlagBits::eGraphics) &&
            physicalDevice.getSurfaceSupportKHR(
                qfpIndex,
                *surface))
        {
            queueIndex = qfpIndex;
            break;
        }
    }

    if (queueIndex == InvalidQueueIndex)
    {
        throw std::runtime_error(
            "Could not find a queue for graphics and present");
    }

    float queuePriority = 1.0f;

    vk::DeviceQueueCreateInfo queueCreateInfo{
        .queueFamilyIndex = queueIndex,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };

    // Base Features
    vk::PhysicalDeviceFeatures2 features{};
    features.features.samplerAnisotropy = VK_TRUE;

    // Vulkan 1.1 Features
    vk::PhysicalDeviceVulkan11Features vulkan11Features{};
    vulkan11Features.shaderDrawParameters = VK_TRUE;

    // Optional feature chain
    void* pNext = nullptr;

    // Extended Dynamic State
    vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
        extendedDynamicStateFeatures{};

    if (appInfo.extendedDynamicStateSupported)
    {
        extendedDynamicStateFeatures.extendedDynamicState =
            VK_TRUE;

        extendedDynamicStateFeatures.pNext = pNext;
        pNext = &extendedDynamicStateFeatures;

        std::cout << "Extended Dynamic State enabled\n";
    }

    // Vulkan 1.3 Features
    vk::PhysicalDeviceVulkan13Features vulkan13Features{};

    bool useVulkan13Features = false;

    if (appInfo.dynamicRenderingSupported)
    {
        vulkan13Features.dynamicRendering = VK_TRUE;
        useVulkan13Features = true;

        std::cout << "Dynamic Rendering enabled\n";
    }

    if (appInfo.synchronization2Supported)
    {
        vulkan13Features.synchronization2 = VK_TRUE;
        useVulkan13Features = true;

        std::cout << "Synchronization2 enabled\n";
    }

    if (useVulkan13Features)
    {
        vulkan13Features.pNext = pNext;
        pNext = &vulkan13Features;
    }

    // Vulkan 1.2 Timeline Semaphores
    vk::PhysicalDeviceTimelineSemaphoreFeatures
        timelineSemaphoreFeatures{};

    if (appInfo.timelineSemaphoresSupported)
    {
        timelineSemaphoreFeatures.timelineSemaphore =
            VK_TRUE;

        timelineSemaphoreFeatures.pNext = pNext;
        pNext = &timelineSemaphoreFeatures;

        std::cout << "Timeline Semaphores enabled\n";
    }

    // Vulkan 1.1 Features chain
    vulkan11Features.pNext = pNext;
    pNext = &vulkan11Features;

    // Attach chain to Features2
    features.pNext = pNext;

    // Device creation
    vk::DeviceCreateInfo deviceCreateInfo{
        .pNext = &features,

        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,

        .enabledExtensionCount =
            static_cast<uint32_t>(
                requiredDeviceExtension.size()),

        .ppEnabledExtensionNames =
            requiredDeviceExtension.data()
    };

    device =
        vk::raii::Device(
            physicalDevice,
            deviceCreateInfo);

    graphicsQueue =
        vk::raii::Queue(
            device,
            queueIndex,
            0);

    graphicsQueueFamilyIndex = queueIndex;

    std::cout << "Logical Device created successfully\n";
}

uint32_t EngineApplication::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const
{
    vk::PhysicalDeviceMemoryProperties memProperties =
        physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}