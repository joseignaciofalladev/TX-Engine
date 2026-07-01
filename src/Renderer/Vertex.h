#pragma once

#include "../Core/VulkanCommon.h"

struct Vertex
{
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;

    static vk::VertexInputBindingDescription getBindingDescription();
    static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions();
    bool operator==(const Vertex& other) const;
};

template<>struct std::hash<Vertex>
{
    size_t operator()(Vertex const& vertex) const noexcept;
};