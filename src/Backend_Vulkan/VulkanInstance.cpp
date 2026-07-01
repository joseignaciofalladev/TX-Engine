#include "../Application/EngineApplication.h"
#include <Core/VulkanConfig.h>

    // ------------------------------------------------------
    // createInstance()
    // ------------------------------------------------------
    // Crea la instancia principal de Vulkan.
    // Equivalente conceptual:
    //
    // DirectX12 -> DXGI Factory
    // OpenGL    -> Contexto OpenGL
    // Vulkan    -> vk::Instance
    //
    // Todo lo demás dependerá de este objeto.
    // La instancia es el objeto raíz de toda aplicación Vulkan.
    //
    // Aplicación
    //  ↓
    // Vulkan Loader
    //  ↓
    // Driver
    //  ↓
    // GPU
    //
    // Antes de crearla:
    // 1. Describimos nuestra aplicación.
    // 2. Comprobamos Validation Layers.
    // 3. Comprobamos extensiones requeridas.
    // 4. Creamos la instancia.
void EngineApplication::createInstance()
{
    // Información descriptiva de la aplicación.
    // Esta información es opcional, pero permite al driver conocer
    // información básica sobre nuestro programa.
    //
    // Vulkan 1.4 es la versión que solicitamos.
    constexpr vk::ApplicationInfo appInfo{
        .pApplicationName = "Hello Triangle",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = vk::ApiVersion14
    };

    // Lista final de capas que queremos habilitar.
    //
    // En Debug: VK_LAYER_KHRONOS_validation
    // En Release: vacío
    std::vector<char const*> requiredLayers;

    if (enableValidationLayers)
    {
        requiredLayers.assign(
            validationLayers.begin(),
            validationLayers.end()
        );
    }

    // Obtenemos todas las capas instaladas en el sistema.
    // Ejemplos:
    //
    // VK_LAYER_KHRONOS_validation
    // VK_LAYER_LUNARG_api_dump
    // ...
    auto layerProperties = context.enumerateInstanceLayerProperties();

    // Verificamos que todas las capas requeridas existen.
    // Si una no existe:
    //
    // - SDK mal instalado
    // - Driver incorrecto
    // - Nombre mal escrito
    auto unsupportedLayerIt =
        std::ranges::find_if(
            requiredLayers,
            [&layerProperties](auto const& requiredLayer)
            {
                return std::ranges::none_of(
                    layerProperties,
                    [requiredLayer](auto const& layerProperty)
                    {
                        return strcmp(
                            layerProperty.layerName,
                            requiredLayer
                        ) == 0;
                    });
            });

    if (unsupportedLayerIt != requiredLayers.end())
    {
        throw std::runtime_error(
            "Required layer not supported: " +
            std::string(*unsupportedLayerIt));
    }

    // Obtenemos las extensiones necesarias.
    // GLFW añade automáticamente las extensiones de ventana.
    // Además añadimos:
    //
    // VK_EXT_debug_utils
    //
    // cuando Validation Layers está activado.
    auto requiredExtensions = getRequiredInstanceExtensions();

    // Enumeramos todas las extensiones disponibles en el sistema.
    auto extensionProperties = context.enumerateInstanceExtensionProperties();

    // Verificamos que todas las extensiones requeridas existen.
    // Si una extensión falta:
    //
    // - Driver demasiado antiguo
    // - SDK incorrecto
    // - Plataforma no compatible
    auto unsupportedPropertyIt =
        std::ranges::find_if(
            requiredExtensions,
            [&extensionProperties](auto const& requiredExtension)
            {
                return std::ranges::none_of(
                    extensionProperties,
                    [requiredExtension](auto const& extensionProperty)
                    {
                        return strcmp(
                            extensionProperty.extensionName,
                            requiredExtension
                        ) == 0;
                    });
            });

    if (unsupportedPropertyIt != requiredExtensions.end())
    {
        throw std::runtime_error(
            "Required extension not supported: " +
            std::string(*unsupportedPropertyIt));
    }

    // Información útil para depuración.
    // Permite comprobar exactamente qué capas y extensiones
    // fueron habilitadas durante el arranque.
    if (enableValidationLayers)
    {
        std::cout << "\nValidation Layers:\n";

        for (auto const* layer : requiredLayers)
        {
            std::cout << "  " << layer << '\n';
        }
    }

    std::cout << "\nInstance Extensions:\n";

    for (auto const* extension : requiredExtensions)
    {
        std::cout << "  " << extension << '\n';
    }

    std::cout << std::endl;

    // Estructura de creación de la instancia.
    // Aquí Vulkan recibe toda la información necesaria:
    //
    // - Datos de la aplicación
    // - Capas activas
    // - Extensiones activas
    vk::InstanceCreateInfo createInfo{
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
        .ppEnabledLayerNames = requiredLayers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
        .ppEnabledExtensionNames = requiredExtensions.data()
    };

    // Creación real de la instancia Vulkan.
    // A partir de este momento ya existe una conexión válida
    // entre nuestra aplicación y el runtime Vulkan.
    instance = vk::raii::Instance(context, createInfo);
}

    // ------------------------------------------------------
    // Vulkan Debug Messenger
    // Recibe todos los mensajes emitidos por:
    //
    // - Validation Layers
    // - Vulkan Loader
    // - Driver
    // 
    // Equivalente a una consola de diagnóstico Vulkan.
    // Ejemplos:
    //
    // INFO
    // Swapchain created
    //
    // WARNING
    // Image layout transition may be inefficient
    //
    // ERROR
    // Descriptor set binding missing
    //
    // Sin Debug Messenger muchos errores son extremadamente
    // difíciles de diagnosticar.
void EngineApplication::setupDebugMessenger()
{
    if (!enableValidationLayers)
        return;

    vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
    vk::DebugUtilsMessageTypeFlagsEXT     messageTypeFlags(
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
    vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{ .messageSeverity = severityFlags,
                                                                          .messageType = messageTypeFlags,
                                                                          .pfnUserCallback = &debugCallback };
    debugMessenger = vk::raii::DebugUtilsMessengerEXT(instance,debugUtilsMessengerCreateInfoEXT);
}

    /*
        Crea una superficie Vulkan desde la ventana de GLFW.
        GLFW gestiona internamente todo el código específico de la plataforma (Win32, X11, Wayland, etc.).
        La superficie resultante representa el destino donde se mostrarán las imágenes renderizadas.
    */
void EngineApplication::createSurface() {
    // GLFW devuelve un identificador temporal de la API Vulkan C (_surface no explica nada)
    VkSurfaceKHR rawSurface;
    if (glfwCreateWindowSurface(*instance, window, nullptr, &rawSurface) != 0) {
        throw std::runtime_error("failed to create window surface!");
    }
    surface = vk::raii::SurfaceKHR(instance, rawSurface);
}

std::vector<const char*> EngineApplication::getRequiredInstanceExtensions()
{
    uint32_t glfwExtensionCount = 0;
    auto     glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    if (enableValidationLayers)
    {
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
    }

    return extensions;
}