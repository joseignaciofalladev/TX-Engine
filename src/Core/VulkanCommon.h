#pragma once

// ------------------------------------------------------
// Vulkan-Hpp Configuration
// ------------------------------------------------------
// Vulkan-Hpp normalmente genera constructores para todas las estructuras Vulkan.
// Al definir esta macro usamos "designated initializers" de C++20:
// vk::ApplicationInfo appInfo{
//     .pApplicationName = "Mi Motor",
//     .apiVersion = vk::ApiVersion14
// };
// Esto hace el código mucho más legible.
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#define NOMINMAX

// Si IntelliSense está analizando el código o no estamos usando módulos C++20,
// incluimos los headers tradicionales.
// vulkan_raii.hpp contiene:
//
// - vk::raii::Instance
// - vk::raii::Device
// - vk::raii::SwapchainKHR
// - etc.
// Estos objetos destruyen automáticamente los recursos Vulkan cuando salen
// de alcance (RAII = Resource Acquisition Is Initialization).
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
// Si el proyecto está configurado con módulos C++20:
// cmake -DENABLE_CPP20_MODULE=ON
// entonces importamos el módulo precompilado.
import vulkan_hpp;
#endif

// ------------------------------------------------------
// GLFW
// ------------------------------------------------------
// GLFW necesita conocer los tipos Vulkan para crear superficies Vulkan.
// GLFW se encargará de incluir el header C de Vulkan.
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

// ------------------------------------------------------
// Librerías estándar
// ------------------------------------------------------
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <cstdint>
#include <map>
#include <algorithm>
#include <assert.h>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>
#include <fstream>
#include <string>
#include <array>
#include <chrono>
#include <filesystem>
#include <unordered_map>
#include <cmath>
#include <random>
#include <thread>
#include <mutex>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>