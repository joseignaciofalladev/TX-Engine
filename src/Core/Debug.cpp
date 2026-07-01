#include "../Application/EngineApplication.h"

VKAPI_ATTR vk::Bool32 VKAPI_CALL EngineApplication::debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void*)
{
    if (severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError || severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
    {
        std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;
    }

    return vk::False;
}