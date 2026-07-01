#pragma once

#include "../Core/VulkanCommon.h"

// Application info structure to store feature support flags
struct AppInfo
{
    uint32_t apiVersion = 0;

    bool dynamicRenderingSupported = false;
    bool timelineSemaphoresSupported = false;
    bool synchronization2Supported = false;
    bool extendedDynamicStateSupported = false;

    bool descriptorIndexingSupported = false;
    bool bufferDeviceAddressSupported = false;
    bool maintenance4Supported = false;
    bool maintenance5Supported = false;

    uint32_t vulkanMajor = 0;
    uint32_t vulkanMinor = 0;
};