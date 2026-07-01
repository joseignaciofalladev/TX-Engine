#pragma once

#include "../Core/VulkanCommon.h"
#include "../Renderer/Vertex.h"
#include "../Renderer/AppInfo.h"
#include <Core/VulkanConfig.h>
#include <Renderer/GameObject.h>

class ThreadSafeResourceManager
{
private:
    std::mutex                           resourceMutex;
    std::vector<vk::raii::CommandPool>   commandPools;
    std::vector<vk::raii::CommandBuffer> commandBuffers;

public:
    void createThreadCommandPools(vk::raii::Device& device, uint32_t queueFamilyIndex, uint32_t threadCount)
    {
        std::lock_guard<std::mutex> lock(resourceMutex);

        commandBuffers.clear();
        commandPools.clear();

        for (uint32_t i = 0; i < threadCount; i++)
        {
            vk::CommandPoolCreateInfo poolInfo{
                .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                .queueFamilyIndex = queueFamilyIndex };
            try
            {
                commandPools.emplace_back(device, poolInfo);
            }
            catch (const std::exception&)
            {
                throw;        // Re-throw the exception to be caught by the caller
            }
        }
    }

    vk::raii::CommandPool& getCommandPool(uint32_t threadIndex)
    {
        std::lock_guard lock(resourceMutex);
        return commandPools[threadIndex];
    }

    void allocateCommandBuffers(vk::raii::Device& device, uint32_t threadCount, uint32_t buffersPerThread)
    {
        std::lock_guard lock(resourceMutex);

        commandBuffers.clear();

        if (commandPools.size() < threadCount)
        {
            throw std::runtime_error("Not enough command pools for thread count");
        }

        for (uint32_t i = 0; i < threadCount; i++)
        {
            vk::CommandBufferAllocateInfo allocInfo{
                .commandPool = *commandPools[i],
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = buffersPerThread };
            try
            {
                auto threadBuffers = device.allocateCommandBuffers(allocInfo);
                for (auto& buffer : threadBuffers)
                {
                    commandBuffers.emplace_back(std::move(buffer));
                }
            }
            catch (const std::exception&)
            {
                throw;        // Re-throw the exception to be caught by the caller
            }
        }
    }

    vk::raii::CommandBuffer& getCommandBuffer(uint32_t index)
    {
        // No need for mutex here as each thread accesses its own command buffer
        if (index >= commandBuffers.size())
        {
            throw std::runtime_error("Command buffer index out of range: " + std::to_string(index) +
                " (available: " + std::to_string(commandBuffers.size()) + ")");
        }
        return commandBuffers[index];
    }
};

class EngineApplication
{
public:
    void run();
private:
    //
    // Flujo principal
    //
    void initWindow();
    void initVulkan();
    void mainLoop();
    void cleanup();
    void cleanupSwapChain();
    void recreateSwapChain();

    //
    // Threads
    //
    void initThreads();
    void workerThreadFunc(uint32_t threadIndex);
    void stopThreads();
    void initThreadResources();
    void signalThreadsToWork();
    void waitForThreadsToComplete();

    //
    // Instance
    //
    void createInstance();
    void setupDebugMessenger();
    void createSurface();

    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
        vk::DebugUtilsMessageTypeFlagsEXT type,
        const vk::DebugUtilsMessengerCallbackDataEXT* callbackData,
        void* userData);

    std::vector<const char*> getRequiredInstanceExtensions();

    //
    // Device
    //
    bool isDeviceSuitable(vk::raii::PhysicalDevice const& physicalDevice);

    void pickPhysicalDevice();
    void detectFeatureSupport();
    void printSelectedGPU();
    void createLogicalDevice();

    uint32_t findMemoryType(
        uint32_t typeFilter,
        vk::MemoryPropertyFlags properties
    ) const;

    //
    // Swapchain
    //
    void createSwapChain();
    void createImageViews();

    static uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& capabilities);
    static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats);
    static vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& modes);

    vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities);

    //
    // Pipelines
    //
    void createDescriptorSetLayout();
    void createGraphicsPipeline();

    vk::raii::ShaderModule createShaderModule(const std::vector<char>& code) const;

    void createDepthResources();
    void createColorResources();

    vk::Format findSupportedFormat(
        const std::vector<vk::Format>& candidates,
        vk::ImageTiling tiling,
        vk::FormatFeatureFlags features);

    vk::Format findDepthFormat();

    static std::vector<char> readFile(const std::string& filename);

    //
    // Images
    //
    void createTextureImage();

    void generateMipmaps(
        vk::raii::Image& image,
        vk::Format format,
        int32_t width,
        int32_t height,
        uint32_t mipLevels);

    void createTextureImageView();
    void createTextureSampler();

    vk::raii::ImageView createImageView(
        vk::Image image,
        vk::Format format,
        vk::ImageAspectFlags aspectFlags,
        uint32_t mipLevels);

    std::pair<vk::raii::Image, vk::raii::DeviceMemory> createImage(
        uint32_t width,
        uint32_t height,
        uint32_t mipLevels,
        vk::SampleCountFlagBits samples,
        vk::Format format,
        vk::ImageTiling tiling,
        vk::ImageUsageFlags usage,
        vk::MemoryPropertyFlags properties);

    void transitionImageLayout(
        vk::raii::CommandBuffer& cmd,
        vk::Image image,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::ImageAspectFlags aspectMask,
        uint32_t baseMipLevel,
        uint32_t levelCount);

    void copyBufferToImage(
        vk::raii::CommandBuffer& commandBuffer,
        const vk::raii::Buffer& buffer,
        vk::raii::Image& image,
        uint32_t width,
        uint32_t height);

    //
    // Models
    //
    void loadModel();

    void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Buffer& buffer, vk::raii::DeviceMemory& bufferMemory) const;

    void createRenderPass();

    void createFramebuffers();

    //
    // Buffers
    //
    void createVertexBuffer();
    void createIndexBuffer();
    void setupGameObjects();
    void createUniformBuffers();

    std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> createBuffer(
        vk::DeviceSize size,
        vk::BufferUsageFlags usage,
        vk::MemoryPropertyFlags properties);

    void copyBuffer(
        vk::raii::Buffer& srcBuffer,
        vk::raii::Buffer& dstBuffer,
        vk::DeviceSize size);

    //
    // Descriptors
    //
    void createDescriptorPool();
    void createDescriptorSets();

    //
    // Commands
    //
    void createCommandPool();
    void createCommandBuffers();

    vk::raii::CommandBuffer beginSingleTimeCommands();

    void endSingleTimeCommands(vk::raii::CommandBuffer&& commandBuffer);
    void recordCommandBuffer(uint32_t imageIndex);

    //
    // Sync
    //
    void createSyncObjects();
    void updateUniformBuffer(uint32_t currentImage);
    void drawFrame();

    //
    // Compute
    //
    void createComputeDescriptorSetLayout();
    void createComputePipeline();
    void createShaderStorageBuffers();
    void createComputeDescriptorSets();
    void createComputeCommandBuffers();
    void recordComputeCommandBuffer(vk::raii::CommandBuffer& cmdBuffer, uint32_t startIndex, uint32_t count);
    void createComputeUniformBuffers();
    void createParticlePipeline();

    //
    // GLFW
    //
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

private:
    // ------------------------------------------------------
    // Ventana GLFW
    // ------------------------------------------------------
    // GLFWwindow es una estructura interna de GLFW.
    // Este puntero representa nuestra ventana del sistema operativo.
    GLFWwindow* window = nullptr;

    // Contexto RAII principal.
    // Equivale al cargador Vulkan.
    // Permite enumerar extensiones, capas e instancias.
    vk::raii::Context context;

    // Instancia Vulkan.
    // Es el objeto raíz de Vulkan.
    // Todo comienza desde aquí.
    vk::raii::Instance instance = nullptr;

    // Mensajero de depuración.
    // Recibe mensajes emitidos por:
    // - Validation Layers
    // - Loader Vulkan
    // - Drivers
    vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;

    // ------------------------------------------------------
    // Physical Device (GPU física)
    // ------------------------------------------------------
    // Representa una GPU REAL instalada en el sistema.
    // - NVIDIA RTX 5090
    // - AMD RX 8900 XTX
    // - Intel Arc
    // - Intel UHD Graphics
    //
    // IMPORTANTE: Esto NO crea una GPU.
    // Vulkan simplemente nos devuelve un manejador para consultar:
    //
    // - capacidades
    // - extensiones
    // - límites
    // - memoria
    // - colas disponibles
    //
    // Más adelante utilizaremos este objeto para crear el Logical Device.
    vk::raii::PhysicalDevice physicalDevice = nullptr;

    // ------------------------------------------------------
    // Dispositivo lógico
    // ------------------------------------------------------
    // Es la interfaz que utiliza nuestra aplicación para comunicarse con la GPU.
    // PhysicalDevice = hardware real
    // Device         = conexión Vulkan con ese hardware
    //
    // La mayoría de recursos Vulkan se crearán usando este objeto.
    vk::raii::Device device = nullptr;

    // ------------------------------------------------------
    // Cola gráfica principal
    // ------------------------------------------------------
    // Vulkan ejecuta trabajo mediante colas (queues).
    // Esta cola será utilizada más adelante para:
    //
    // - Renderizado
    // - Envío de command buffers
    // - Presentación de imágenes
    //
    // Actualmente sólo necesitamos una cola gráfica.
    vk::raii::Queue graphicsQueue = nullptr;

    /*
        Conexión entre Vulkan y la ventana del sistema operativo.
        Se requiere un SurfaceKHR para mostrar las imágenes renderizadas
        en la pantalla.
        Se crea desde la ventana GLFW y se utiliza posteriormente
        al crear la cadena de intercambio.
        La creación de la superficie debe realizarse antes de seleccionar una GPU,
        ya que la compatibilidad con la presentación depende de ello.
     */
    vk::raii::SurfaceKHR surface = nullptr;

    vk::raii::SwapchainKHR           swapChain = nullptr;

    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline graphicsPipeline = nullptr;

    vk::raii::Image        depthImage = nullptr;
    vk::raii::DeviceMemory depthImageMemory = nullptr;
    vk::raii::ImageView    depthImageView = nullptr;

    uint32_t               mipLevels = 1;
    vk::raii::Image        textureImage = nullptr;
    vk::raii::DeviceMemory textureImageMemory = nullptr;
    vk::raii::ImageView    textureImageView = nullptr;
    vk::raii::Sampler      textureSampler = nullptr;

    vk::raii::DescriptorPool             descriptorPool = nullptr;

    vk::raii::CommandPool    commandPool = nullptr;
    // Porque cada frame necesita su propio command buffer
    std::vector<vk::raii::CommandBuffer> commandBuffers;

    vk::raii::Buffer       vertexBuffer = nullptr;
    vk::raii::DeviceMemory vertexBufferMemory = nullptr;
    vk::raii::Buffer       indexBuffer = nullptr;
    vk::raii::DeviceMemory indexBufferMemory = nullptr;

    // Imagen adquirida de la swapchain y lista para renderizar
    std::vector<vk::raii::Semaphore> imageAvailableSemaphores;
    // Renderizado finalizado y listo para presentar
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores;

    // Fence usada para sincronizar CPU ↔ GPU
    std::vector<vk::raii::Fence> inFlightFences;
    std::vector<vk::Fence> imagesInFlight;

    std::unordered_map<Vertex, uint32_t> uniqueVertices{};

    std::vector<vk::ImageLayout> swapChainLayouts;
    std::vector<vk::Image>           swapChainImages;
    vk::SurfaceFormatKHR             swapChainSurfaceFormat;
    vk::Extent2D                     swapChainExtent;

    // Vista de cada imagen perteneciente a la swapchain.
    // Las ImageView describen cómo acceder a las imágenes durante el renderizado.
    std::vector<vk::raii::ImageView> swapChainImageViews;

    // ------------------------------------------------------
    // Required Device Extensions
    // ------------------------------------------------------
    /*
        Las extensiones de instancia afectan a Vulkan globalmente.
        Las extensiones de dispositivo afectan únicamente a la GPU seleccionada.
        Actualmente sólo necesitamos:

         VK_KHR_swapchain

         Esta extensión es obligatoria para poder presentar imágenes
         en pantalla.

         Sin ella:
         GPU -> Renderiza
         GPU -> Produce imágenes

         PERO: No podemos mostrarlas en una ventana.
    */
    std::vector<const char*> requiredDeviceExtension = {
        vk::KHRSwapchainExtensionName,
        vk::KHRSynchronization2ExtensionName
    };

    vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1;

    vk::raii::Image colorImage = nullptr;
    vk::raii::DeviceMemory colorImageMemory = nullptr;
    vk::raii::ImageView colorImageView = nullptr;

    vk::SampleCountFlagBits getMaxUsableSampleCount()
    {
        auto properties = physicalDevice.getProperties();

        vk::SampleCountFlags counts =
            properties.limits.framebufferColorSampleCounts &
            properties.limits.framebufferDepthSampleCounts;

        if (counts & vk::SampleCountFlagBits::e64)
            return vk::SampleCountFlagBits::e64;

        if (counts & vk::SampleCountFlagBits::e32)
            return vk::SampleCountFlagBits::e32;

        if (counts & vk::SampleCountFlagBits::e16)
            return vk::SampleCountFlagBits::e16;

        if (counts & vk::SampleCountFlagBits::e8)
            return vk::SampleCountFlagBits::e8;

        if (counts & vk::SampleCountFlagBits::e4)
            return vk::SampleCountFlagBits::e4;

        if (counts & vk::SampleCountFlagBits::e2)
            return vk::SampleCountFlagBits::e2;

        return vk::SampleCountFlagBits::e1;
    }

    uint32_t graphicsQueueFamilyIndex = UINT32_MAX;

    // Frame actualmente utilizado.
    // Va rotando entre 0 y MAX_FRAMES_IN_FLIGHT-1.
    uint32_t frameIndex = 0;

    bool framebufferResized = false;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    std::array<GameObject, MAX_OBJECTS> gameObjects;

    AppInfo appInfo;

    vk::raii::RenderPass renderPass{ nullptr };
    std::vector<vk::raii::Framebuffer> swapChainFramebuffers;

    double lastFrameTime = 0.0;
    double lastTime = 0.0f;

    std::vector<vk::raii::Buffer> computeUniformBuffers;
    std::vector<vk::raii::DeviceMemory> computeUniformBuffersMemory;
    std::vector<void*> computeUniformBuffersMapped;

    vk::raii::DescriptorSetLayout computeDescriptorSetLayout = nullptr;
    vk::raii::PipelineLayout computePipelineLayout = nullptr;
    vk::raii::Pipeline computePipeline = nullptr;
    vk::raii::DescriptorPool computeDescriptorPool = nullptr;
    std::vector<vk::raii::DescriptorSet> computeDescriptorSets;
    std::vector<vk::raii::CommandBuffer> computeCommandBuffers;
    std::vector<vk::raii::Buffer>       shaderStorageBuffers;
    std::vector<vk::raii::DeviceMemory> shaderStorageBuffersMemory;

    vk::raii::PipelineLayout particlePipelineLayout = nullptr;
    vk::raii::Pipeline particlePipeline = nullptr;
    std::vector<vk::raii::Semaphore> computeFinishedSemaphores;

    uint32_t                       threadCount = 0;
    std::vector<std::thread>       workerThreads;
    std::atomic<bool>              shouldExit{ false };
    std::vector<std::atomic<bool>> threadWorkReady;
    std::vector<std::atomic<bool>> threadWorkDone;

    std::mutex              queueSubmitMutex;
    std::mutex              workCompleteMutex;
    std::condition_variable workCompleteCv;

    ThreadSafeResourceManager resourceManager;
    struct ParticleGroup
    {
        uint32_t startIndex;
        uint32_t count;
    };
    std::vector<ParticleGroup> particleGroups;
};