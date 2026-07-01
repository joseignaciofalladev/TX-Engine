#include "../Application/EngineApplication.h"
#include <Core/VulkanConfig.h>

// ------------------------------------------------------
    // Crea el Command Pool asociado a la cola gráfica.
    //
    // El Command Pool es el objeto encargado de administrar la memoria utilizada
    // por los Command Buffers. Todos los buffers de comandos deben ser creados
    // desde un pool.
    //
    // Cada Command Pool está ligado a una familia de colas específica
    // (queue family). Como nuestro Command Buffer ejecutará comandos gráficos,
    // utilizamos la familia de colas gráfica.
    //
    // eResetCommandBuffer permite reiniciar y volver a grabar cada Command Buffer
    // individualmente. Sin esta bandera tendríamos que reiniciar todo el pool.
    // ------------------------------------------------------
void EngineApplication::createCommandPool()
{
    vk::CommandPoolCreateInfo poolInfo{ .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                       .queueFamilyIndex = graphicsQueueFamilyIndex };
    commandPool = vk::raii::CommandPool(device, poolInfo);
}

vk::raii::CommandBuffer EngineApplication::beginSingleTimeCommands()
{
    vk::CommandBufferAllocateInfo allocInfo{ .commandPool = commandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1 };
    vk::raii::CommandBuffer commandBuffer = std::move(vk::raii::CommandBuffers(device, allocInfo).front());

    vk::CommandBufferBeginInfo beginInfo{ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit };
    commandBuffer.begin(beginInfo);

    return commandBuffer;
}

void EngineApplication::endSingleTimeCommands(vk::raii::CommandBuffer&& commandBuffer)
{
    commandBuffer.end();

    vk::FenceCreateInfo fenceInfo{};
    vk::raii::Fence fence(device, fenceInfo);

    vk::CommandBufferSubmitInfo commandBufferInfo{
        .commandBuffer = *commandBuffer,
        .deviceMask = 0
    };

    vk::SubmitInfo2 submitInfo{
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferInfo
    };

    graphicsQueue.submit2(
        submitInfo,
        *fence
    );

    vk::Result result =
        device.waitForFences(
            *fence,
            VK_TRUE,
            UINT64_MAX);

    if (result != vk::Result::eSuccess)
    {
        throw std::runtime_error(
            "Failed waiting for upload fence");
    }
}

// ============================================================================
    // Reserva un Command Buffer primario desde el Command Pool.
    //
    // Los Command Buffers almacenan todos los comandos que la GPU ejecutará:
    //  - Barreras
    //  - Cambios de estado
    //  - Draw calls
    //  - Dispatches
    //  - Copias de memoria
    //
    // Vulkan distingue entre:
    //
    //  ePrimary
    //      Puede enviarse directamente a una cola mediante queue.submit().
    //
    //  eSecondary
    //      No puede ejecutarse directamente.
    //      Debe ser llamado desde un Command Buffer primario.
    // 
    // Reservamos un command buffer por frame en vuelo.
    // Así la CPU puede grabar el siguiente frame mientras
    // la GPU sigue ejecutando el anterior.
void EngineApplication::createCommandBuffers()
{
    commandBuffers.clear();
    vk::CommandBufferAllocateInfo allocInfo{ .commandPool = commandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = MAX_FRAMES_IN_FLIGHT };
    commandBuffers = vk::raii::CommandBuffers(device, allocInfo);
}

// -----------------------------------------------------------------------------
    // Graba todos los comandos necesarios para renderizar un frame.
    //
    // imageIndex:
    //     Índice de la imagen de la swapchain que vamos a utilizar
    //     durante este frame.
    //
    // Flujo:
    //
    // 1. Iniciar grabación del Command Buffer.
    // 2. Cambiar el layout de la imagen a ColorAttachment.
    // 3. Configurar el attachment de renderizado.
    // 4. Iniciar Dynamic Rendering.
    // 5. Configurar pipeline, viewport y scissor.
    // 6. Dibujar el triángulo.
    // 7. Finalizar Dynamic Rendering.
    // 8. Cambiar layout para presentación.
    // 9. Finalizar grabación del Command Buffer.
    // -----------------------------------------------------------------------------
void EngineApplication::recordCommandBuffer(uint32_t imageIndex)
{
    auto& commandBuffer = commandBuffers[frameIndex];
    commandBuffer.begin({});

    transitionImageLayout(
        commandBuffer,
        swapChainImages[imageIndex],
        swapChainLayouts[imageIndex],
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageAspectFlagBits::eColor, 0, 1);

    swapChainLayouts[imageIndex] = vk::ImageLayout::eColorAttachmentOptimal;

    vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);

    std::array<vk::ClearValue, 2> clearValues = {
        clearColor,
        clearDepth
    };

    vk::RenderingAttachmentInfo colorAttachment{
        .imageView = colorImageView,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,

        .resolveMode = vk::ResolveModeFlagBits::eAverage,
        .resolveImageView = swapChainImageViews[imageIndex],
        .resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal,

        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = clearColor
    };

    vk::RenderingAttachmentInfo depthAttachment{
        .imageView = depthImageView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = clearDepth
    };

    vk::RenderingInfo renderingInfo{
        .renderArea = {{0, 0}, swapChainExtent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment
    };

    commandBuffer.beginRendering(renderingInfo);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);

    commandBuffer.setViewport(
        0,
        vk::Viewport{
            0.0f, 0.0f,
            static_cast<float>(swapChainExtent.width),
            static_cast<float>(swapChainExtent.height),
            0.0f, 1.0f
        }
    );

    commandBuffer.setScissor(0, vk::Rect2D{ {0, 0}, swapChainExtent });
    commandBuffer.bindVertexBuffers(0, *vertexBuffer, { 0 });
    commandBuffer.bindIndexBuffer(*indexBuffer, 0, vk::IndexTypeValue<decltype(indices)::value_type>::value);

    // Draw each object with its own descriptor set
    for (const auto& gameObject : gameObjects){
        // Bind the descriptor set for this object
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, *gameObject.descriptorSets[frameIndex], nullptr);

        // Draw the object
        commandBuffer.drawIndexed(indices.size(), 1, 0, 0, 0);
    }

    commandBuffer.bindPipeline(
        vk::PipelineBindPoint::eGraphics,
        *particlePipeline);

    commandBuffer.bindVertexBuffers(
        0,
        { *shaderStorageBuffers[frameIndex] },
        { 0 });

    commandBuffer.draw(
        PARTICLE_COUNT,
        1,
        0,
        0);

    commandBuffer.endRendering();

    transitionImageLayout(
        commandBuffer,
        swapChainImages[imageIndex],
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR,
        vk::ImageAspectFlagBits::eColor, 0, 1);

    swapChainLayouts[imageIndex] = vk::ImageLayout::ePresentSrcKHR;

    commandBuffer.end();
}