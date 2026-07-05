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
    computeFinishedSemaphores.clear();
    renderFinishedSemaphores.clear();
    inFlightFences.clear();

    imageAvailableSemaphores.reserve(MAX_FRAMES_IN_FLIGHT);
    computeFinishedSemaphores.reserve(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.reserve(swapChainImages.size());
    inFlightFences.reserve(MAX_FRAMES_IN_FLIGHT);

    const vk::SemaphoreCreateInfo semaphoreInfo{};
    const vk::FenceCreateInfo fenceInfo{.flags = vk::FenceCreateFlagBits::eSignaled};

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        imageAvailableSemaphores.emplace_back(device, semaphoreInfo);
        computeFinishedSemaphores.emplace_back(device, semaphoreInfo);
        inFlightFences.emplace_back(device, fenceInfo);
    }

    for (uint32_t i = 0; i < static_cast<uint32_t>(swapChainImages.size()); ++i)
    {
        renderFinishedSemaphores.emplace_back(device, semaphoreInfo);
    }
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
