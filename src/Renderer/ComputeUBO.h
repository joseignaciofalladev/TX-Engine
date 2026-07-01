#pragma once

#include "../Core/VulkanCommon.h"

struct ComputeUBO
{
    alignas(4) float deltaTime = 0.0f;

    // Padding para mantener alineación std140
    alignas(4) float padding0 = 0.0f;
    alignas(4) float padding1 = 0.0f;
    alignas(4) float padding2 = 0.0f;
};