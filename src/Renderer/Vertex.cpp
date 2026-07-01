#include "Vertex.h"

vk::VertexInputBindingDescription Vertex::getBindingDescription()
{
    return { .binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex };
}

std::array<vk::VertexInputAttributeDescription, 3>
Vertex::getAttributeDescriptions()
{
    return { {{.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, pos)},
           {.location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(Vertex, color)},
           {.location = 2, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(Vertex, texCoord)}} };
}

bool Vertex::operator==(const Vertex& other) const
{
    return
        pos == other.pos &&
        color == other.color &&
        texCoord == other.texCoord;
}

size_t std::hash<Vertex>::operator()(Vertex const& vertex) const noexcept
{
    return (
        (
            hash<glm::vec3>()(vertex.pos) ^
            (hash<glm::vec3>()(vertex.color) << 1)
            ) >> 1
        ) ^
        (hash<glm::vec2>()(vertex.texCoord) << 1);
}