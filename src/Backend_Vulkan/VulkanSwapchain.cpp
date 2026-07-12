#include "../Application/EngineApplication.h"

void EngineApplication::createSwapChain()
{
    // Consultar las capacidades de presentación de la superficie.
    // Estas capacidades incluyen límites de resolución, número de imágenes
    // permitidas y transformaciones soportadas por la ventana.
    vk::SurfaceCapabilitiesKHR surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);

    //------------------------------------------------------
    // Choose swapchain settings
    //------------------------------------------------------
    swapChainExtent = chooseSwapExtent(surfaceCapabilities);
    uint32_t minImageCount = chooseSwapMinImageCount(surfaceCapabilities);

    // Obtener todos los formatos de imagen compatibles con la superficie.
    // Cada formato define el layout de color y el espacio de color.
    std::vector<vk::SurfaceFormatKHR> availableFormats = physicalDevice.getSurfaceFormatsKHR(*surface);
    swapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);

    // Porque algunos drivers tienen un límite máximo.
    if (surfaceCapabilities.maxImageCount > 0 &&
        minImageCount > surfaceCapabilities.maxImageCount)
    {
        minImageCount = surfaceCapabilities.maxImageCount;
    }

    // Obtener los modos de presentación soportados.
    // El modo de presentación determina cómo se sincronizan
    // las imágenes renderizadas con la pantalla.
    std::vector<vk::PresentModeKHR> availablePresentModes = physicalDevice.getSurfacePresentModesKHR(*surface);
    vk::PresentModeKHR              presentMode = chooseSwapPresentMode(availablePresentModes);

    // Crear la cadena de intercambio (swapchain).
    // La swapchain gestionará las imágenes que se renderizarán
    // y posteriormente se presentarán en pantalla.
    vk::SwapchainCreateInfoKHR swapChainCreateInfo{ .surface = *surface,
                                                   .minImageCount = minImageCount,
                                                   .imageFormat = swapChainSurfaceFormat.format,
                                                   .imageColorSpace = swapChainSurfaceFormat.colorSpace,
                                                   .imageExtent = swapChainExtent,
                                                   .imageArrayLayers = 1,
                                                   .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,

        // El modo exclusivo es más rápido cuando los gráficos y
        // la presentación utilizan la misma familia de colas.
        .imageSharingMode = vk::SharingMode::eExclusive,

        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = presentMode,
        .clipped = true,
        .oldSwapchain = nullptr };

    // Recuperar los handles de las imágenes creadas internamente
    // por la swapchain. Estas imágenes serán utilizadas más adelante
    // como destino del renderizado.
    swapChain = vk::raii::SwapchainKHR(device, swapChainCreateInfo);
    swapChainImages = swapChain.getImages();
    imagesInFlight.clear();

    imagesInFlight.resize(
        swapChainImages.size(),
        VK_NULL_HANDLE
    );

    swapChainLayouts.assign(swapChainImages.size(), vk::ImageLayout::eUndefined);

    // Comprobar que realmente se obtuvo imágenes
    if (swapChainImages.empty())
    {
        throw std::runtime_error(
            "Failed to retrieve swapchain images!");
    }
}

// Crear una ImageView para cada imagen de la swapchain.
    // Estas vistas serán utilizadas posteriormente como attachments
    // dentro del pipeline de renderizado.
void EngineApplication::createImageViews()
{
    assert(swapChainImageViews.empty());

    swapChainImageViews.reserve(swapChainImages.size());
    for (auto& image : swapChainImages)
    {
        swapChainImageViews.emplace_back(createImageView(image, swapChainSurfaceFormat.format, vk::ImageAspectFlagBits::eColor, 1));
    }
}

/*
    ===============================================================================
    cleanupSwapChain
    ===============================================================================

    Libera todos los recursos que dependen directamente de la SwapChain.

    Actualmente sólo existen:

        - swapChainImageViews
        - swapChain

    Pero más adelante también podrían incluirse:

        - Depth Images
        - Depth Image Views
        - MSAA Images
        - Framebuffers
        - Render Targets auxiliares

    Esta función se llama antes de recrear la SwapChain para garantizar que
    ningún recurso antiguo permanezca vivo utilizando imágenes inválidas.

    IMPORTANTE:
    Debe ejecutarse únicamente cuando la GPU haya terminado de usar dichos
    recursos (device.waitIdle()).

    ===============================================================================
    */
void EngineApplication::cleanupSwapChain()
{
    commandBuffers.clear();

    graphicsPipeline = nullptr;
    pipelineLayout = nullptr;

    particlePipeline = nullptr;
    particlePipelineLayout = nullptr;

    // computePipeline = nullptr;
    // computePipelineLayout = nullptr;

    colorImageView = nullptr;
    colorImage = nullptr;
    colorImageMemory = nullptr;

    depthImageView = nullptr;
    depthImage = nullptr;
    depthImageMemory = nullptr;

    swapChainImageViews.clear();

    swapChainFramebuffers.clear();
    renderPass.clear();

    swapChainLayouts.clear();
    swapChainImages.clear();

    imagesInFlight.clear();

    swapChain = nullptr;
}

/*
    ===============================================================================
    recreateSwapChain
    ===============================================================================

    Reconstruye todos los recursos dependientes del tamaño de la ventana.

    Se ejecuta cuando:

        - La ventana cambia de tamaño.
        - La SwapChain se vuelve obsoleta (eErrorOutOfDateKHR).
        - La SwapChain es subóptima (eSuboptimalKHR).
        - La ventana vuelve desde un estado minimizado.

    Proceso:

        1) Esperar a que la ventana tenga un tamaño válido.
        2) Esperar a que la GPU termine cualquier trabajo pendiente.
        3) Liberar recursos antiguos de la SwapChain.
        4) Crear una nueva SwapChain.
        5) Crear nuevas Image Views asociadas.

    Caso especial: ventana minimizada

    Cuando una ventana se minimiza, GLFW devuelve:

        width  = 0
        height = 0

    Vulkan no permite crear una SwapChain de tamaño cero.

    Por ello esperamos hasta que el usuario restaure la ventana.

    Flujo:

        Resize / Minimize Event
                ↓
        framebufferResized = true
                ↓
        drawFrame()
                ↓
        recreateSwapChain()
                ↓
        cleanupSwapChain()
                ↓
        createSwapChain()
                ↓
        createImageViews()

    Más adelante esta función también recreará:

        - Depth Buffers
        - MSAA Buffers
        - Render Targets
        - Recursos dependientes de resolución

    ===============================================================================
    */
void EngineApplication::recreateSwapChain()
{
    int width = 0;
    int height = 0;

    glfwGetFramebufferSize(window, &width, &height);

    while (width == 0 || height == 0)
    {
        glfwWaitEvents();
        glfwGetFramebufferSize(window, &width, &height);
    }

    device.waitIdle();

    cleanupSwapChain();

    // Swapchain
    createSwapChain();
    createImageViews();

    // Attachments
    createColorResources();
    createDepthResources();

    // Pipelines
    createGraphicsPipeline();
    createParticlePipeline();
    // createComputePipeline();

    // Command buffers
    createCommandBuffers();
}

vk::Extent2D EngineApplication::chooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    return {
        std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height) };
}

uint32_t EngineApplication::chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& surfaceCapabilities)
{
    auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
    if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount))
    {
        minImageCount = surfaceCapabilities.maxImageCount;
    }
    return minImageCount;
}

vk::SurfaceFormatKHR EngineApplication::chooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const& availableFormats)
{
    assert(!availableFormats.empty());
    const auto formatIt = std::ranges::find_if(
        availableFormats,
        [](const auto& format) { return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; });
    return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
}

vk::PresentModeKHR EngineApplication::chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const& availablePresentModes)
{
    assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) { return presentMode == vk::PresentModeKHR::eFifo; }));
    return std::ranges::any_of(availablePresentModes,
        [](const vk::PresentModeKHR value) { return vk::PresentModeKHR::eMailbox == value; }) ?
        vk::PresentModeKHR::eMailbox :
        vk::PresentModeKHR::eFifo;
}
