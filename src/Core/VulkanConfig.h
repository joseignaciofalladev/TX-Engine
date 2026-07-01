#pragma once

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;
constexpr int MAX_OBJECTS = 3;

constexpr uint32_t PARTICLE_COUNT = 8192;

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

inline const std::vector<char const*> validationLayers ={"VK_LAYER_KHRONOS_validation"};

inline const std::string MODEL_PATH = "models/viking_room.obj";
inline const std::string TEXTURE_PATH = "textures/viking_room.png";