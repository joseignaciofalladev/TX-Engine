#include "../Application/EngineApplication.h"
#include <Renderer/GraphicsUBO.h>
#include <Core/VulkanConfig.h>
#include <Renderer/ComputeUBO.h>

// -----------------------------------------------------------------------------
   // Crea todos los objetos de sincronización utilizados por el renderizador.
   //
   // Objetos creados:
   //
   // presentCompleteSemaphore
   //     Se señaliza cuando la swapchain entrega una imagen lista para renderizar.
   //
   // renderFinishedSemaphore
   //     Se señaliza cuando la GPU termina de ejecutar los comandos de dibujo.
   //
   // drawFence
   //     Permite que la CPU espere a que la GPU termine el frame actual antes de
   //     reutilizar recursos como el command buffer.
   //
   // La fence se crea inicialmente en estado "signaled" para que el primer frame
   // no se quede bloqueado esperando un trabajo que nunca se ha enviado.
   // -----------------------------------------------------------------------------
void EngineApplication::createSyncObjects()
{
    imageAvailableSemaphores.clear();
    renderFinishedSemaphores.clear();
    computeFinishedSemaphores.clear();
    computeFinishedSemaphores.reserve(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.reserve(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.clear();

    imageAvailableSemaphores.reserve(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.reserve(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        imageAvailableSemaphores.emplace_back(device, vk::SemaphoreCreateInfo{});
        computeFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo{});
        inFlightFences.emplace_back(device, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
    }

    for (size_t i = 0;i < swapChainImages.size();i++)
    {
        renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo{});
    }
}

void EngineApplication::updateUniformBuffer(uint32_t currentFrame)
{
    static auto startTime = std::chrono::high_resolution_clock::now();
    static auto lastFrameTime = startTime;
    auto        currentTime = std::chrono::high_resolution_clock::now();
    float       time = std::chrono::duration<float>(currentTime - startTime).count();
    float       deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
    lastFrameTime = currentTime;

    // Camera and projection matrices (shared by all objects)
    glm::mat4 view = glm::lookAt(glm::vec3(2.0f, 2.0f, 6.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height), 0.1f, 20.0f);
    proj[1][1] *= -1;

    // Update uniform buffers for each object
    for (auto& gameObject : gameObjects)
    {
        // Apply continuous rotation to the object based on frame time
        const float rotationSpeed = 0.5f;                          // Rotation speed in radians per second
        gameObject.rotation.y += rotationSpeed * deltaTime;        // Slow rotation around Y axis scaled by frame time

        // Get the model matrix for this object
        glm::mat4 model = gameObject.getModelMatrix();

        // Create and update the UBO
        GraphicsUBO ubo{
            .model = model,
            .view = view,
            .proj = proj };

        // Copy the UBO data to the mapped memory
        memcpy(gameObject.uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
    }
    ComputeUBO computeUBO{};
    computeUBO.deltaTime = deltaTime;

    memcpy(computeUniformBuffersMapped[currentFrame], &computeUBO, sizeof(ComputeUBO));
}

// Renderiza un frame completo.
//
// Flujo:
// 1. Esperar a que termine el frame anterior.
// 2. Obtener una imagen libre de la swapchain.
// 3. Grabar el command buffer.
// 4. Enviar el trabajo a la GPU.
// 5. Presentar la imagen en pantalla.

void EngineApplication::drawFrame()
{
    auto fenceResult = device.waitForFences(*inFlightFences[frameIndex], VK_TRUE, UINT64_MAX);

    if (fenceResult != vk::Result::eSuccess)
    {
        throw std::runtime_error("failed to wait for fence!");
    }

    // Recreate swapchain if window resized
    if (framebufferResized)
    {
        recreateSwapChain();
        framebufferResized = false;
        return;
    }

    auto [result, imageIndex] = swapChain.acquireNextImage( UINT64_MAX, *imageAvailableSemaphores[frameIndex], nullptr);

    if (result == vk::Result::eErrorOutOfDateKHR)
    {
        recreateSwapChain();
        return;
    }

    else if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
    {
        assert(result == vk::Result::eTimeout || result == vk::Result::eNotReady);
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    if (imagesInFlight[imageIndex] != VK_NULL_HANDLE)
    {
        auto result = device.waitForFences(imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }

    imagesInFlight[imageIndex] = vk::Fence(*inFlightFences[frameIndex]);

    updateUniformBuffer(frameIndex);

    signalThreadsToWork();

    recordCommandBuffer(imageIndex);

    waitForThreadsToComplete();
    
    std::vector<vk::CommandBuffer> computeCmdBuffers;
    computeCmdBuffers.reserve(threadCount);
    
    for (uint32_t i = 0; i < threadCount; i++)
    {
        computeCmdBuffers.push_back(*resourceManager.getCommandBuffer(i));
    }

    if (computeCmdBuffers.empty())
        return;

    vk::SubmitInfo computeSubmit{};
    computeSubmit.commandBufferCount = static_cast<uint32_t>(computeCmdBuffers.size());
    computeSubmit.pCommandBuffers = computeCmdBuffers.data();

    computeSubmit.signalSemaphoreCount = 1;
    computeSubmit.pSignalSemaphores = &*computeFinishedSemaphores[frameIndex];

    graphicsQueue.submit(computeSubmit, nullptr);

    device.resetFences(*inFlightFences[frameIndex]);

    vk::SemaphoreSubmitInfo waitSemaphoreInfo[2]
    {
        {
            .semaphore = *imageAvailableSemaphores[frameIndex],
            .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput
        },
        {
            .semaphore = *computeFinishedSemaphores[frameIndex],
            .stageMask = vk::PipelineStageFlagBits2::eVertexInput
        }
    };

    vk::CommandBufferSubmitInfo commandBufferInfo{
        .commandBuffer = *commandBuffers[frameIndex]
    };

    vk::SemaphoreSubmitInfo signalSemaphoreInfo{
        .semaphore = *renderFinishedSemaphores[imageIndex],
        .stageMask = vk::PipelineStageFlagBits2::eAllGraphics
    };
    
    vk::SubmitInfo2 submitInfo{
        .waitSemaphoreInfoCount = 2,
        .pWaitSemaphoreInfos = waitSemaphoreInfo,

        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferInfo,

        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signalSemaphoreInfo
    };
    
    graphicsQueue.submit2(submitInfo, *inFlightFences[frameIndex]);

    vk::PresentInfoKHR presentInfo {
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*renderFinishedSemaphores[imageIndex],
        .swapchainCount = 1,
        .pSwapchains = &*swapChain,
        .pImageIndices = &imageIndex
    };

    result = graphicsQueue.presentKHR(presentInfo);

    if ((result == vk::Result::eSuboptimalKHR) || (result == vk::Result::eErrorOutOfDateKHR) || framebufferResized)
    {
        framebufferResized = false;
        recreateSwapChain();
    } else {
        assert(result == vk::Result::eSuccess);
    }

    frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}
