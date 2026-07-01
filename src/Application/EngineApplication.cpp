#include "EngineApplication.h"
#include "../Core/VulkanConfig.h"

// ------------------------------------------------------
// Punto de entrada de la aplicación
// ------------------------------------------------------
// Organiza la ejecución en etapas:
//
// 1. Crear ventana
// 2. Inicializar Vulkan
// 3. Ejecutar bucle principal
// 4. Liberar recursos
void EngineApplication::run()
{
    initWindow();
    initVulkan();
    initThreads();
    mainLoop();
    cleanup();
}

// ------------------------------------------------------
    // Inicialización de ventana
    // ------------------------------------------------------
void EngineApplication::initWindow()
{
    // Inicializa toda la librería GLFW.
    // Internamente:
    // 
    // - registra clases de ventana
    // - configura entrada de teclado
    // - configura ratón
    // - prepara eventos del sistema
    glfwInit();

    // GLFW fue diseñado originalmente para OpenGL.
    // Esta línea le dice:
    // "NO me crees un contexto OpenGL"
    // Porque vamos a usar Vulkan.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // Crea una ventana real del sistema operativo.
    // Parámetros:
    //
    // WIDTH      -> ancho
    // HEIGHT     -> alto
    // "Vulkan"   -> título
    // nullptr    -> monitor (modo ventana)
    // nullptr    -> ventana compartida (OpenGL)
    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);

    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

    // Si la ventana no pudo crearse, abortamos inmediatamente.
    // Vulkan no puede funcionar sin superficie de presentación.
    if (!window)
    {
        throw std::runtime_error(
            "Failed to create GLFW window"
        );
    }
}

/*
    ===============================================================================
    framebufferResizeCallback
    ===============================================================================

    Callback de GLFW que se ejecuta automáticamente cuando cambia el tamaño
    del framebuffer de la ventana.

    IMPORTANTE:
    No recreamos la SwapChain aquí directamente porque GLFW puede llamar a este
    callback mientras Vulkan está renderizando un frame. Recrear recursos Vulkan
    desde el callback podría provocar condiciones de carrera o recursos en uso.

    Lo único que hacemos es marcar framebufferResized = true.

    Más adelante, drawFrame() comprobará este flag y recreará la SwapChain
    de forma segura al finalizar la presentación actual.

    Parámetros:
        window -> ventana GLFW que generó el evento.
        width  -> nuevo ancho del framebuffer.
        height -> nueva altura del framebuffer.

    Flujo:
        GLFW resize event
            ↓
        framebufferResizeCallback()
            ↓
        framebufferResized = true
            ↓
        drawFrame()
            ↓
        recreateSwapChain()

    ===============================================================================
    */
void EngineApplication::framebufferResizeCallback(GLFWwindow* window, int width, int height)
{
    auto app = reinterpret_cast<EngineApplication*>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
}

// ------------------------------------------------------
// Inicialización Vulkan
// ------------------------------------------------------
// Aquí irán todos los objetos Vulkan.
void EngineApplication::initVulkan()
{
    std::cout << "createInstance\n";
    createInstance();
    std::cout << "setupDebugMessenger\n";
    setupDebugMessenger();
    std::cout << "createSurface\n";
    createSurface();
    std::cout << "pickPhysicalDevice\n";
    pickPhysicalDevice();
    std::cout << "detectFeatureSupport\n";
    detectFeatureSupport();
    std::cout << "printSelectedGPU\n";
    printSelectedGPU();
    std::cout << "createLogicalDevice\n";
    createLogicalDevice();
    std::cout << "createSwapChain\n";
    createSwapChain();
    std::cout << "createImageViews\n";
    createImageViews();

    std::cout << "createRenderPass o createFramebuffers\n";
    if (!appInfo.dynamicRenderingSupported)
    {
        std::cout << "createRenderPass\n";
        createRenderPass();
        std::cout << "createFramebuffers\n";
        createFramebuffers();
    }

    std::cout << "createDescriptorSetLayout\n";
    createDescriptorSetLayout();
    std::cout << "createComputeDescriptorSetLayout\n";
    createComputeDescriptorSetLayout();
    std::cout << "createGraphicsPipeline\n";
    createGraphicsPipeline();
    std::cout << "createParticlePipeline\n";
    createParticlePipeline();
    std::cout << "createComputePipeline\n";
    createComputePipeline();
    std::cout << "createCommandPool\n";
    createCommandPool();
    std::cout << "createShaderStorageBuffers\n";
    createShaderStorageBuffers();
    std::cout << "createColorResources\n";
    createColorResources();
    std::cout << "createDepthResources\n";
    createDepthResources();
    std::cout << "createTextureImage\n";
    createTextureImage();
    std::cout << "createTextureImageView\n";
    createTextureImageView();
    std::cout << "createTextureSampler\n";
    createTextureSampler();
    std::cout << "loadModel\n";
    loadModel();
    std::cout << "createVertexBuffer\n";
    createVertexBuffer();
    std::cout << "createIndexBuffer\n";
    createIndexBuffer();
    std::cout << "setupGameObjects\n";
    setupGameObjects();
    std::cout << "createUniformBuffers\n";
    createUniformBuffers();
    std::cout << "createComputeUniformBuffers\n";
    createComputeUniformBuffers();
    std::cout << "createDescriptorPool\n";
    createDescriptorPool();
    std::cout << "createComputeDescriptorSets\n";
    createComputeDescriptorSets();
    std::cout << "createDescriptorSets\n";
    createDescriptorSets();
    std::cout << "createCommandBuffers\n";
    createCommandBuffers();
    std::cout << "createComputeCommandBuffers\n";
    createComputeCommandBuffers();
    std::cout << "createSyncObjects\n";
    createSyncObjects();

    // Print feature support summary
    std::cout << "\nFeature support summary:\n";
    std::cout << "- Dynamic Rendering: " << (appInfo.dynamicRenderingSupported ? "Yes" : "No") << "\n";
    std::cout << "- Timeline Semaphores: " << (appInfo.timelineSemaphoresSupported ? "Yes" : "No") << "\n";
    std::cout << "- Synchronization2: " << (appInfo.synchronization2Supported ? "Yes" : "No") << "\n";
}

// ------------------------------------------------------
// Bucle principal
// ------------------------------------------------------
// Un motor gráfico suele ejecutarse:
// while(running)
// {
//     Input
//     Update
//     Render
// }
void EngineApplication::mainLoop()
{
    // lastFrameTime = glfwGetTime();

    while (!glfwWindowShouldClose(window))
    {
        // Procesar eventos del sistema operativo
        glfwPollEvents();

        // Calcular deltaTime (futuro)
        /*
        double currentTime = glfwGetTime();
        deltaTime = static_cast<float>(currentTime - lastFrameTime);
        lastFrameTime = currentTime;
        */

        // Actualizar entrada (futuro)
        // inputSystem.update();

        // Actualizar lógica del motor (futuro)
        // update(deltaTime);

        // Actualizar física (futuro)
        // physicsSystem.update(deltaTime);

        // Actualizar IA (futuro)
        // aiSystem.update(deltaTime);

        // Actualizar audio (futuro)
        // audioSystem.update();

        // Renderizar frame
        drawFrame();

        // Limitar FPS (opcional futuro)
        // frameLimiter.wait();
    }

    if (*device)
    {
        device.waitIdle();
    }
}

// ------------------------------------------------------
    // Liberación de recursos
    // ------------------------------------------------------
    // Todo lo que creemos debe destruirse.
    // En Vulkan esto es extremadamente importante.
    // No hace falta tocar nada más.
void EngineApplication::cleanup()
{
    stopThreads();

    if (*device)
    {
        device.waitIdle();
    }

    // Clean up resources in each GameObject
    for (auto& gameObject : gameObjects)
    {
        // Unmap memory
        for (size_t i = 0; i < gameObject.uniformBuffersMemory.size(); i++)
        {
            if (gameObject.uniformBuffersMapped[i] != nullptr)
            {
                gameObject.uniformBuffersMemory[i].unmapMemory();
            }
        }

        // Clear vectors to release resources
        gameObject.uniformBuffers.clear();
        gameObject.uniformBuffersMemory.clear();
        gameObject.uniformBuffersMapped.clear();
        gameObject.descriptorSets.clear();
    }

    // Destruye la ventana.
    if (window)
    {
        glfwDestroyWindow(window);
        window = nullptr;
    }

    // Libera GLFW completamente.
    glfwTerminate();
}