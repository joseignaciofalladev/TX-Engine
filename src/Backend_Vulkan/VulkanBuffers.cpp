#include "../Application/EngineApplication.h"
#include <Renderer/GraphicsUBO.h>
#include <Core/VulkanConfig.h>
#include <tiny_obj_loader.h>
#include <Renderer/GameObject.h>

/*
        El staging buffer es visible para CPU.

        Se mapea temporalmente para copiar los datos
        de vértices desde memoria del programa hacia
        memoria Vulkan.

        Posteriormente los datos serán transferidos
        al buffer definitivo de GPU.
    */
void EngineApplication::createVertexBuffer()
{
    vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    auto [stagingBuffer, stagingBufferMemory] =
        createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, vertices.data(), bufferSize);
    stagingBufferMemory.unmapMemory();

    /*
        Buffer final de vértices.

        Se almacena en memoria local del dispositivo
        para maximizar el rendimiento de lectura
        durante el renderizado.

        No es accesible directamente desde CPU.
    */
    std::tie(vertexBuffer, vertexBufferMemory) =
        createBuffer(bufferSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

    copyBuffer(stagingBuffer, vertexBuffer, bufferSize);
}

/*
        Crea un buffer Vulkan y reserva la memoria necesaria.

        Parámetros:
            size       -> tamaño total del buffer en bytes
            usage      -> uso previsto del buffer
            properties -> propiedades de memoria deseadas

        Flujo:

            1. Crear vk::Buffer
            2. Consultar requisitos de memoria
            3. Buscar un tipo de memoria compatible
            4. Reservar memoria
            5. Asociar memoria al buffer

        Devuelve:
            Buffer + DeviceMemory asociados.
    */
std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> EngineApplication::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties)
{
    vk::BufferCreateInfo   bufferInfo{ .size = size, .usage = usage, .sharingMode = vk::SharingMode::eExclusive };
    vk::raii::Buffer       buffer = vk::raii::Buffer(device, bufferInfo);
    vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{ .allocationSize = memRequirements.size, .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties) };
    vk::raii::DeviceMemory bufferMemory = vk::raii::DeviceMemory(device, allocInfo);
    buffer.bindMemory(*bufferMemory, 0);
    return { std::move(buffer), std::move(bufferMemory) };
}

/*
        Crea un buffer de índices en memoria Device Local.

        Los índices se copian primero a un staging buffer
        accesible por CPU y posteriormente se transfieren
        al buffer final optimizado para lectura por GPU.
    */
void EngineApplication::createIndexBuffer()
{
    vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    auto [stagingBuffer, stagingBufferMemory] =
        createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* data = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(data, indices.data(), (size_t)bufferSize);
    stagingBufferMemory.unmapMemory();

    std::tie(indexBuffer, indexBufferMemory) =
        createBuffer(bufferSize, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

    copyBuffer(stagingBuffer, indexBuffer, bufferSize);
}

void EngineApplication::setupGameObjects()
{
    // Object 1 - Center
    gameObjects[0].position = { 0.0f, 0.0f, 0.0f };
    gameObjects[0].rotation = { 0.0f, glm::radians(-90.0f), 0.0f };
    gameObjects[0].scale = { 1.0f, 1.0f, 1.0f };

    // Object 2 - Left
    gameObjects[1].position = { -2.0f, 0.0f, -1.0f };
    gameObjects[1].rotation = { 0.0f, glm::radians(-45.0f), 0.0f };
    gameObjects[1].scale = { 0.75f, 0.75f, 0.75f };

    // Object 3 - Right
    gameObjects[2].position = { 2.0f, 0.0f, -1.0f };
    gameObjects[2].rotation = { 0.0f, glm::radians(45.0f), 0.0f };
    gameObjects[2].scale = { 0.75f, 0.75f, 0.75f };
}

void EngineApplication::createUniformBuffers()
{
    vk::DeviceSize bufferSize = sizeof(GraphicsUBO);

    for (auto& gameObject : gameObjects)
    {
        gameObject.uniformBuffers.reserve(MAX_FRAMES_IN_FLIGHT);
        gameObject.uniformBuffersMemory.reserve(MAX_FRAMES_IN_FLIGHT);
        gameObject.uniformBuffersMapped.reserve(MAX_FRAMES_IN_FLIGHT);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            auto [buffer, memory] =
                createBuffer(
                    bufferSize,
                    vk::BufferUsageFlagBits::eUniformBuffer,
                    vk::MemoryPropertyFlagBits::eHostVisible |
                    vk::MemoryPropertyFlagBits::eHostCoherent);

            gameObject.uniformBuffers.emplace_back(std::move(buffer));
            gameObject.uniformBuffersMemory.emplace_back(std::move(memory));

            gameObject.uniformBuffersMapped.push_back(
                gameObject.uniformBuffersMemory.back().mapMemory(
                    0,
                    bufferSize));
        }
    }
}

void EngineApplication::createDescriptorPool()
{
    std::array<vk::DescriptorPoolSize, 3> poolSize{
        vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT + MAX_FRAMES_IN_FLIGHT},
        vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT},
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer,MAX_FRAMES_IN_FLIGHT * 2}
    };
    vk::DescriptorPoolCreateInfo          poolInfo{.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                                   .maxSets = MAX_OBJECTS * MAX_FRAMES_IN_FLIGHT + MAX_FRAMES_IN_FLIGHT,
                                                   .poolSizeCount = static_cast<uint32_t>(poolSize.size()),
                                                   .pPoolSizes = poolSize.data() };
    descriptorPool = vk::raii::DescriptorPool(device, poolInfo);
}

void EngineApplication::createDescriptorSets()
{
    // For each game object
    for (auto& gameObject : gameObjects)
    {
        // Create descriptor sets for each frame in flight
        std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout);
        vk::DescriptorSetAllocateInfo        allocInfo{
                   .descriptorPool = *descriptorPool,
                   .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
                   .pSetLayouts = layouts.data() };

        gameObject.descriptorSets.clear();
        gameObject.descriptorSets = device.allocateDescriptorSets(allocInfo);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            vk::DescriptorBufferInfo bufferInfo{
                .buffer = *gameObject.uniformBuffers[i],
                .offset = 0,
                .range = sizeof(GraphicsUBO) };
            vk::DescriptorImageInfo imageInfo{
                .sampler = *textureSampler,
                .imageView = *textureImageView,
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
            std::array descriptorWrites{
                vk::WriteDescriptorSet{
                    .dstSet = *gameObject.descriptorSets[i],
                    .dstBinding = 0,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eUniformBuffer,
                    .pBufferInfo = &bufferInfo},
                vk::WriteDescriptorSet{
                    .dstSet = *gameObject.descriptorSets[i],
                    .dstBinding = 1,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                    .pImageInfo = &imageInfo} };
            device.updateDescriptorSets(descriptorWrites, {});
        }
    }
}

void EngineApplication::copyBuffer(vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer, vk::DeviceSize size)
{
    auto cmd = beginSingleTimeCommands();

    cmd.copyBuffer(
        *srcBuffer,
        *dstBuffer,
        vk::BufferCopy{
            .srcOffset = 0,
            .dstOffset = 0,
            .size = size
        });

    endSingleTimeCommands(std::move(cmd));
}

void EngineApplication::loadModel()
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    std::string warn;
    std::string err;

    if (!tinyobj::LoadObj(
        &attrib,
        &shapes,
        &materials,
        &warn,
        &err,
        MODEL_PATH.c_str()))
    {
        throw std::runtime_error(warn + err);
    }

    size_t originalVertexCount = 0;
    for (const auto& shape : shapes)
    {
        for (const auto& index : shape.mesh.indices)
        {
            Vertex vertex{};

            vertex.pos = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };

            if (index.texcoord_index >= 0)
            {
                vertex.texCoord = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                };
            }
            else
            {
                vertex.texCoord = { 0.0f,0.0f };
            }

            vertex.color = { 1.0f, 1.0f, 1.0f };

            originalVertexCount++;
            if (uniqueVertices.count(vertex) == 0)
            {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }

            indices.push_back(uniqueVertices[vertex]);
        }
    }
    std::cout << "indices: " << indices.size() << std::endl;
    std::cout << "UBO Size = " << sizeof(GraphicsUBO) << std::endl;

    std::cout << "\nModelo cargado:\n";
    std::cout << "Vertices originales: " << originalVertexCount << '\n';
    std::cout << "Vertices unicos:      " << vertices.size() << '\n';

    float reduction = 100.0f * (1.0f - static_cast<float>(vertices.size()) / static_cast<float>(originalVertexCount));

    std::cout << "Reduccion: " << reduction << "%\n";
}

void EngineApplication::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Buffer& buffer, vk::raii::DeviceMemory& bufferMemory) const
{
    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;
    buffer = vk::raii::Buffer(device, bufferInfo);
    vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
    bufferMemory = vk::raii::DeviceMemory(device, allocInfo);
    buffer.bindMemory(bufferMemory, 0);
}

void EngineApplication::createRenderPass()
{
    if (appInfo.dynamicRenderingSupported)
    {
        // No render pass needed with dynamic rendering
        std::cout << "Using dynamic rendering, skipping render pass creation\n";
        return;
    }

    std::cout << "Creating traditional render pass\n";

    // Color attachment description
    vk::AttachmentDescription colorAttachment{
        .format = swapChainSurfaceFormat.format,
        .samples = msaaSamples,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = vk::ImageLayout::eUndefined,
        .finalLayout = vk::ImageLayout::eColorAttachmentOptimal };

    vk::AttachmentDescription depthAttachment{
        .format = findDepthFormat(),
        .samples = msaaSamples,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = vk::ImageLayout::eUndefined,
        .finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal };

    vk::AttachmentDescription colorAttachmentResolve{
        .format = swapChainSurfaceFormat.format,
        .samples = vk::SampleCountFlagBits::e1,
        .loadOp = vk::AttachmentLoadOp::eDontCare,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = vk::ImageLayout::eUndefined,
        .finalLayout = vk::ImageLayout::ePresentSrcKHR };

    // Subpass references
    vk::AttachmentReference colorAttachmentRef{
        .attachment = 0,
        .layout = vk::ImageLayout::eColorAttachmentOptimal };

    vk::AttachmentReference depthAttachmentRef{
        .attachment = 1,
        .layout = vk::ImageLayout::eDepthStencilAttachmentOptimal };

    vk::AttachmentReference colorAttachmentResolveRef{
        .attachment = 2,
        .layout = vk::ImageLayout::eColorAttachmentOptimal };

    // Subpass description
    vk::SubpassDescription subpass{
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentRef,
        .pResolveAttachments = &colorAttachmentResolveRef,
        .pDepthStencilAttachment = &depthAttachmentRef };

    // Dependency to ensure proper image layout transitions
    vk::SubpassDependency dependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
        .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
        .srcAccessMask = vk::AccessFlagBits::eNone,
        .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite };

    // Create the render pass
    std::array               attachments = { colorAttachment, depthAttachment, colorAttachmentResolve };
    vk::RenderPassCreateInfo renderPassInfo{
        .attachmentCount = static_cast<uint32_t>(attachments.size()),
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency };

    renderPass = vk::raii::RenderPass(device, renderPassInfo);
}

void EngineApplication::createFramebuffers()
{
    if (appInfo.dynamicRenderingSupported)
    {
        // No framebuffers needed with dynamic rendering
        std::cout << "Using dynamic rendering, skipping framebuffer creation\n";
        return;
    }

    std::cout << "Creating traditional framebuffers\n";

    swapChainFramebuffers.clear();

    for (size_t i = 0; i < swapChainImageViews.size(); i++)
    {
        std::array attachments = {
            *colorImageView,
            *depthImageView,
            *swapChainImageViews[i] };

        vk::FramebufferCreateInfo framebufferInfo{
            .renderPass = *renderPass,
            .attachmentCount = static_cast<uint32_t>(attachments.size()),
            .pAttachments = attachments.data(),
            .width = swapChainExtent.width,
            .height = swapChainExtent.height,
            .layers = 1 };

        swapChainFramebuffers.emplace_back(device, framebufferInfo);
    }
}