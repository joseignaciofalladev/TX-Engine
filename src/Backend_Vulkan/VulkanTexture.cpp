#include "../Application/EngineApplication.h"
#include <Core/VulkanConfig.h>
#include <stb_image.h>

vk::Format EngineApplication::findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features)
{
    for (const auto format : candidates)
    {
        vk::FormatProperties props = physicalDevice.getFormatProperties(format);
        if (((tiling == vk::ImageTiling::eLinear) && ((props.linearTilingFeatures & features) == features)) ||
            ((tiling == vk::ImageTiling::eOptimal) && ((props.optimalTilingFeatures & features) == features)))
        {
            return format;
        }
    }

    throw std::runtime_error("failed to find supported format!");
}

vk::Format EngineApplication::findDepthFormat()
{
    return findSupportedFormat({ vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint },
        vk::ImageTiling::eOptimal,
        vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

void EngineApplication::createTextureImage()
{
    int texWidth, texHeight, texChannels;

    stbi_uc* pixels =
        stbi_load(
            TEXTURE_PATH.c_str(),
            &texWidth,
            &texHeight,
            &texChannels,
            STBI_rgb_alpha);

    if (!pixels)
    {
        throw std::runtime_error(
            "failed to load texture image!");
    }

    vk::DeviceSize imageSize =
        static_cast<vk::DeviceSize>(texWidth) *
        static_cast<vk::DeviceSize>(texHeight) *
        4;

    mipLevels = static_cast<uint32_t>(
        std::floor(
            std::log2(
                std::max(texWidth, texHeight)
            )
        )
        ) + 1;

    if (texWidth <= 0 || texHeight <= 0)
    {
        stbi_image_free(pixels);
        throw std::runtime_error("Invalid texture size");
    }

    auto [stagingBuffer, stagingBufferMemory] =
        createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* data = stagingBufferMemory.mapMemory(0, imageSize);
    memcpy(data, pixels, imageSize);
    stagingBufferMemory.unmapMemory();

    stbi_image_free(pixels);

    std::tie(textureImage, textureImageMemory) = createImage(
        texWidth,
        texHeight,
        mipLevels,
        vk::SampleCountFlagBits::e1,
        vk::Format::eR8G8B8A8Srgb,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferSrc |
        vk::ImageUsageFlagBits::eTransferDst |
        vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal
    );

    vk::raii::CommandBuffer commandBuffer = beginSingleTimeCommands();
    transitionImageLayout(
        commandBuffer,
        *textureImage,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageAspectFlagBits::eColor,
        0, mipLevels);
    copyBufferToImage(commandBuffer, stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
    endSingleTimeCommands(std::move(commandBuffer));
    generateMipmaps(textureImage, vk::Format::eR8G8B8A8Srgb, texWidth, texHeight, mipLevels);
}

void EngineApplication::generateMipmaps(
    vk::raii::Image& image,
    vk::Format imageFormat,
    int32_t texWidth,
    int32_t texHeight,
    uint32_t mipLevels)
{
    vk::FormatProperties formatProperties =
        physicalDevice.getFormatProperties(imageFormat);

    if (!(formatProperties.optimalTilingFeatures &
        vk::FormatFeatureFlagBits::eSampledImageFilterLinear))
    {
        throw std::runtime_error(
            "Texture image format does not support linear blitting!");
    }

    auto commandBuffer = beginSingleTimeCommands();

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < mipLevels; i++)
    {
        //
        // mip i-1:
        // TRANSFER_DST -> TRANSFER_SRC
        //
        vk::ImageMemoryBarrier2 barrierToSrc{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,

            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferRead,

            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eTransferSrcOptimal,

            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

            .image = *image,

            .subresourceRange{
                vk::ImageAspectFlagBits::eColor,
                i - 1,
                1,
                0,
                1
            }
        };

        vk::DependencyInfo dependencyInfo1{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrierToSrc
        };

        commandBuffer.pipelineBarrier2(dependencyInfo1);

        //
        // Blit mip i-1 -> mip i
        //
        vk::ImageBlit blit{
            .srcSubresource =
            {
                vk::ImageAspectFlagBits::eColor,
                i - 1,
                0,
                1
            },

            .srcOffsets = std::array{
                vk::Offset3D{0, 0, 0},
                vk::Offset3D{mipWidth, mipHeight, 1}
            },

            .dstSubresource =
            {
                vk::ImageAspectFlagBits::eColor,
                i,
                0,
                1
            },

            .dstOffsets = std::array{
                vk::Offset3D{0, 0, 0},
                vk::Offset3D{
                    mipWidth > 1 ? mipWidth / 2 : 1,
                    mipHeight > 1 ? mipHeight / 2 : 1,
                    1
                }
            }
        };

        commandBuffer.blitImage(
            *image,
            vk::ImageLayout::eTransferSrcOptimal,

            *image,
            vk::ImageLayout::eTransferDstOptimal,

            blit,

            vk::Filter::eLinear);

        //
        // mip i-1:
        // TRANSFER_SRC -> SHADER_READ
        //
        vk::ImageMemoryBarrier2 barrierToShader{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferRead,

            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,

            .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,

            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

            .image = *image,

            .subresourceRange{
                vk::ImageAspectFlagBits::eColor,
                i - 1,
                1,
                0,
                1
            }
        };

        vk::DependencyInfo dependencyInfo2{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrierToShader
        };

        commandBuffer.pipelineBarrier2(dependencyInfo2);

        if (mipWidth > 1)
            mipWidth /= 2;

        if (mipHeight > 1)
            mipHeight /= 2;
    }

    //
    // Último mip:
    // TRANSFER_DST -> SHADER_READ
    //
    vk::ImageMemoryBarrier2 finalBarrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,

        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,

        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,

        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

        .image = *image,

        .subresourceRange{
            vk::ImageAspectFlagBits::eColor,
            mipLevels - 1,
            1,
            0,
            1
        }
    };

    vk::DependencyInfo dependencyInfo3{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &finalBarrier
    };

    commandBuffer.pipelineBarrier2(dependencyInfo3);

    endSingleTimeCommands(std::move(commandBuffer));
}

void EngineApplication::createTextureImageView()
{
    textureImageView =
        createImageView(
            *textureImage,
            vk::Format::eR8G8B8A8Srgb,
            vk::ImageAspectFlagBits::eColor,
            mipLevels);
}

void EngineApplication::createTextureSampler()
{
    vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
    vk::SamplerCreateInfo        samplerInfo{ .magFilter = vk::Filter::eLinear,
                                             .minFilter = vk::Filter::eLinear,
                                             .mipmapMode = vk::SamplerMipmapMode::eLinear,
                                             .addressModeU = vk::SamplerAddressMode::eRepeat,
                                             .addressModeV = vk::SamplerAddressMode::eRepeat,
                                             .addressModeW = vk::SamplerAddressMode::eRepeat,
                                             .mipLodBias = 0.0f,
                                             .anisotropyEnable = vk::True,
                                             .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
                                             .compareEnable = vk::False,
                                             .compareOp = vk::CompareOp::eAlways,
                                             .minLod = 0.0f,
                                             .maxLod = static_cast<float>(mipLevels),
                                             .borderColor = vk::BorderColor::eIntOpaqueBlack,
                                             .unnormalizedCoordinates = VK_FALSE };
    textureSampler = vk::raii::Sampler(device, samplerInfo);
}

void EngineApplication::createColorResources()
{
    auto colorFormat = swapChainSurfaceFormat.format;

    std::tie(colorImage, colorImageMemory) =
        createImage(
            swapChainExtent.width,
            swapChainExtent.height,
            1,
            msaaSamples,
            colorFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransientAttachment |
            vk::ImageUsageFlagBits::eColorAttachment,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

    colorImageView = createImageView(*colorImage, colorFormat, vk::ImageAspectFlagBits::eColor, 1);

    auto cmd = beginSingleTimeCommands();

    transitionImageLayout(
        cmd,
        *colorImage,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageAspectFlagBits::eColor, 0, 1
    );

    endSingleTimeCommands(std::move(cmd));
}

void EngineApplication::createDepthResources()
{
    vk::Format depthFormat = findDepthFormat();

    std::tie(depthImage, depthImageMemory) = createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
    depthImageView = createImageView(depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);

    vk::raii::CommandBuffer cmd = beginSingleTimeCommands();

    transitionImageLayout(
        cmd,
        *depthImage,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eDepthAttachmentOptimal,
        vk::ImageAspectFlagBits::eDepth,
        0, 1
    );

    endSingleTimeCommands(std::move(cmd));
}

vk::raii::ImageView EngineApplication::createImageView(
    vk::Image image,
    vk::Format format,
    vk::ImageAspectFlags aspectFlags,
    uint32_t mipLevels
)
{
    vk::ImageViewCreateInfo viewInfo{
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = aspectFlags,
            .baseMipLevel = 0,
            .levelCount = mipLevels,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    return vk::raii::ImageView(device, viewInfo);
}

std::pair<vk::raii::Image, vk::raii::DeviceMemory> EngineApplication::createImage(
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels,
    vk::SampleCountFlagBits samples,
    vk::Format format,
    vk::ImageTiling tiling,
    vk::ImageUsageFlags usage,
    vk::MemoryPropertyFlags properties)
{
    vk::ImageCreateInfo imageInfo{ .imageType = vk::ImageType::e2D,
                                  .format = format,
                                  .extent = {width, height, 1},
                                  .mipLevels = mipLevels,
                                  .arrayLayers = 1,
                                  .samples = samples,
                                  .tiling = tiling,
                                  .usage = usage,
                                  .sharingMode = vk::SharingMode::eExclusive };

    vk::raii::Image image = vk::raii::Image(device, imageInfo);

    vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{ .allocationSize = memRequirements.size,
                                     .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties) };
    vk::raii::DeviceMemory imageMemory = vk::raii::DeviceMemory(device, allocInfo);
    image.bindMemory(imageMemory, 0);

    return { std::move(image), std::move(imageMemory) };
}

void EngineApplication::transitionImageLayout(
    vk::raii::CommandBuffer& cmd,
    vk::Image image,
    vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout,
    vk::ImageAspectFlags aspectMask,
    uint32_t baseMipLevel,
    uint32_t levelCount)
{
    vk::ImageMemoryBarrier2 barrier{
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange =
        {
            .aspectMask = aspectMask,
            .baseMipLevel = baseMipLevel,
            .levelCount = levelCount,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    if (oldLayout == vk::ImageLayout::eUndefined &&
        newLayout == vk::ImageLayout::eTransferDstOptimal)
    {
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
        barrier.srcAccessMask = {};

        barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
    }
    else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
        newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
    {
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;

        barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
    }
    else if (oldLayout == vk::ImageLayout::eUndefined &&
        newLayout == vk::ImageLayout::eDepthAttachmentOptimal)
    {
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
        barrier.srcAccessMask = {};

        barrier.dstStageMask =
            vk::PipelineStageFlagBits2::eEarlyFragmentTests |
            vk::PipelineStageFlagBits2::eLateFragmentTests;

        barrier.dstAccessMask =
            vk::AccessFlagBits2::eDepthStencilAttachmentRead |
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    }
    else if (oldLayout == vk::ImageLayout::eUndefined &&
        newLayout == vk::ImageLayout::eColorAttachmentOptimal)
    {
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
        barrier.srcAccessMask = {};

        barrier.dstStageMask =
            vk::PipelineStageFlagBits2::eColorAttachmentOutput;

        barrier.dstAccessMask =
            vk::AccessFlagBits2::eColorAttachmentWrite;
    }
    else if (
        oldLayout == vk::ImageLayout::eColorAttachmentOptimal &&
        newLayout == vk::ImageLayout::ePresentSrcKHR)
    {
        barrier.srcStageMask =
            vk::PipelineStageFlagBits2::eColorAttachmentOutput;

        barrier.srcAccessMask =
            vk::AccessFlagBits2::eColorAttachmentWrite;

        barrier.dstStageMask = vk::PipelineStageFlagBits2::eNone;

        barrier.dstAccessMask = {};
    }
    else if (
        oldLayout == vk::ImageLayout::ePresentSrcKHR &&
        newLayout == vk::ImageLayout::eColorAttachmentOptimal)
    {
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eNone;

        barrier.srcAccessMask = {};

        barrier.dstStageMask =
            vk::PipelineStageFlagBits2::eColorAttachmentOutput;

        barrier.dstAccessMask =
            vk::AccessFlagBits2::eColorAttachmentWrite;
    }
    else
    {
        throw std::runtime_error("Unsupported layout transition");
    }

    vk::DependencyInfo dependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };

    cmd.pipelineBarrier2(dependencyInfo);
}

void EngineApplication::copyBufferToImage(vk::raii::CommandBuffer& commandBuffer, const vk::raii::Buffer& buffer, vk::raii::Image& image, uint32_t width, uint32_t height)
{
    vk::BufferImageCopy region{ .bufferOffset = 0,
                               .bufferRowLength = 0,
                               .bufferImageHeight = 0,
                               .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
                               .imageOffset = {0, 0, 0},
                               .imageExtent = {width, height, 1} };
    commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, region);
}