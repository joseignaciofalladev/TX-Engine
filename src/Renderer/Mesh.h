#pragma once

#include "../Core/VulkanCommon.h"

struct Mesh
{
    vk::raii::Buffer vertexBuffer = nullptr;
    vk::raii::DeviceMemory vertexMemory = nullptr;

    vk::raii::Buffer indexBuffer = nullptr;
    vk::raii::DeviceMemory indexMemory = nullptr;

    uint32_t indexCount = 0;
};