#pragma once

#include "../Core/VulkanCommon.h"

struct GraphicsUBO
{
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};